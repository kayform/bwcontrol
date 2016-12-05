#include "postgres.h"
#include "fmgr.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "lib/stringinfo.h"

#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>
#include <curl/curl.h>
#include <string.h>
#include <dlfcn.h>
#include <stdlib.h>

#include "error_string.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_add_ingest_table);
PG_FUNCTION_INFO_V1(pg_del_ingest_table);
PG_FUNCTION_INFO_V1(pg_resume_ingest);
PG_FUNCTION_INFO_V1(pg_suspend_ingest);
PG_FUNCTION_INFO_V1(pg_get_status_ingest);
PG_FUNCTION_INFO_V1(pg_create_kafka_connect);
PG_FUNCTION_INFO_V1(pg_delete_kafka_connect);

void _PG_init(void);
void _PG_fini(void);

/* ------------------------------------------------
 * Maros
 * ------------------------------------------------*/

#define DB_LOG_RETURN(level, str, err) do {     \
	char err_buff[256];							\
	snprintf(err_buff, 255, "%s(%d)", str, err);\
	elog(level, "%s", err_buff);						\
	PG_RETURN_TEXT_P(cstring_to_text(err_buff));\
} while(0)

#define xpfree(var_) \
    do { \
        if (var_ != NULL) \
        { \
            pfree(var_); \
            var_ = NULL; \
        } \
    } while (0)

#define xfree(var_) \
    do { \
        if (var_ != NULL) \
        { \
            free(var_); \
            var_ = NULL; \
        } \
    } while (0)

#define ALLOCNCOPY(ptr_, var_) \
    do { \
		int len = 0; \
        if (var_ != NULL){ \
			len = strlen(var_)+1 \
            ptr_ = (char*)malloc(len); \
			memset(ptr_, 0x00, len); \
			strncpy(ptr_, var_, len); \
		} \
    } while (0)

#define CHECKMAXLEN(val_, len) \
    do { \
        if (val_ != NULL) \
        { \
            if(strlen(val_) >= len) \
				PG_RETURN_TEXT_P(cstring_to_text("Check string length."));\
        } \
    } while (0)

#define CHECK_RETURN(err, call) { err = call; if (err < 0) return err;}
#define CHECK(err, call) err = call

#define MODE_INSERT  0
#define MODE_UPDATE  1
#define MODE_DELETE  2
#define MODE_SUSPEND 3
#define MODE_RESUME  4

#define QBUFFLEN 10240
#define CONTENTSDATALEN 10240
#define URLLEN 512
#define OPTLEN 256
#define INVALID_PID (-1)

#define CONTENT_TYPE "application/json"
#define HTTP_TIMOUT 3

/* ------------------------------------------------
 * define queries 						
 * ------------------------------------------------*/
#define CHECK_TABLE_EXISTS "SELECT tablename FROM pg_catalog.pg_tables WHERE schemaname = '%s' AND tablename = '%s'"
#define GET_DATABASE_INFO "select user, current_database() from pg_stat_activity where pg_stat_activity.pid=pg_backend_pid()"
#define GET_REPLICATION_SETTING "select current_setting('bw.bwpath') bwpath, current_setting('bw.kafka_broker') kafka_broker, current_setting('bw.schema_registry') schema_registry, current_setting('bw.consumer') consumer, current_setting('bw.consumer_sub') consumer_sub"
//#define INSERT_MAP_TABLE "INSERT INTO tbl_mapps SELECT A.table_catalog database_name, A.table_schema, A.table_name, B.relnamespace, B.oid reloid, A.table_catalog || '-' || A.table_name as topic_name, now() create_date, current_user create_user, '' remark FROM information_schema.tables A inner join pg_class B on A.table_name = B.relname where table_name='%s'"
// If the Postgres schema name is 'public', we just set topic_name with the table_name.
// schema  = 'public' : topic_name = table name
// schema != 'public' : topic_name = table_schema.table name
#define INSERT_MAP_TABLE "INSERT INTO tbl_mapps (database_name, table_schema, table_name, relnamespace, reloid, topic_name, create_date, create_user, remark) \
SELECT A.table_catalog database_name, A.table_schema, \
A.table_name, B.relnamespace, B.oid reloid, \
(select case when A.table_schema = 'public' then A.table_name else A.table_schema || '.' || A.table_name end) as topic_name, \
now() create_date, current_user create_user, '' remark \
FROM information_schema.tables A inner join pg_class B on A.table_name = B.relname \
where table_name='%s' and A.table_schema = '%s' \
and B.oid = (SELECT '%s.%s'::regclass::oid) \
and not exists (select 1 from tbl_mapps C where C.table_name = '%s' and C.table_schema = '%s');"
#define DELETE_MAP_TABLE "DELETE FROM tbl_mapps WHERE table_schema = '%s' and table_name = '%s'"
#define INSERT_KAFKA_CONFIG "INSERT INTO kafka_con_config VALUES(current_database(), '%s', '%s'::json->'config', now(), current_user, '')"
#define UPDATE_KAFKA_CONFIG "UPDATE kafka_con_config SET contents = '%s' WHERE connect_name = '%s'"
#define DELETE_KAFKA_CONFIG "DELETE from kafka_con_config"
#define GET_KAFKA_CONN_INFO "SELECT connect_name, contents FROM kafka_con_config"
#define GET_TOPIC_LIST_EXCEPT_PARAM "SELECT topic_name FROM tbl_mapps WHERE table_name != '%s'"
#define GET_TOPIC_LIST "SELECT topic_name FROM tbl_mapps"
#define GENERATE_TOPICS "select jsonb_set('%s'::jsonb, '{topics}', '\"%s\"'::jsonb)"
#define GENERATE_CONNECT_NAME "select jsonb_set(f1,'{\"config\", \"hdfs.url\"}', f2::jsonb) from (select result f1, to_json((result::json->>'config')::json->>'hdfs.url'||'%s') f2 from jsonb_set(('%s'::jsonb-'tasks')::jsonb#-'{\"config\",\"name\"}', '{name}', '\"%s\"'::jsonb) result) result2"

/* ------------------------------------------------
 * structures 						
 * ------------------------------------------------*/
typedef struct custom_config{
	char bwpath[MAXPGPATH];		   /* Bottledwater intalled path */
	char broker[URLLEN];           /* Kafka broker URL */
	char schema_registry[URLLEN];  /* Schema Registry URL */
	char consumer[URLLEN];         /* Kafka connect URL */
	char consumer_sub[URLLEN]; /* Kafka connect sub URL for default config */
} custom_config_t;

struct MemoryStruct {
  char *memory;
  size_t size;
};

/* ------------------------------------------------
 * internal functions						
 * ------------------------------------------------*/
int check_bw_process(const char* db_name);
int control_process(const char* db_name, const char* db_user, const char* hostname, const char* conn_info, const custom_config_t* config, int mode);
int check_exists_table(const char * schema_name, const char * table_name);
int	get_kafka_connect_info(char* conn_name, char *contents);
int remove_replication_slot(const char* db_name);
int update_mapping_table(const char * schema_name, const char * table_name, bool operation);
int get_database_info(char *db_user, char *db_name);
int get_custom_config(custom_config_t *pconfig);
static size_t response_cb(void *contents, size_t size, size_t nmemb, void *userp);
pid_t spawn_bw_process(const char *cmdline);
int sync_with_kafka_connect(const char* db_name, const char* table_name, const char* conn_info, const custom_config_t* config);
int update_kafka_conn_info(unsigned char mode, const char *contents, const char *conn_name);
int http_get_kafka_connect(char *contents, const custom_config_t *config);
int http_set_kafka_connect(const char *conn_name, const char *contents, const custom_config_t *config);
int http_create_kafka_connect(const char *conn_name, const char *contents, const custom_config_t *config);
int http_delete_kafka_connect(const char *conn_name, const char *contents, const custom_config_t *config);
int get_topic_list(const char* conn_name, const char* table_name, char **topics);
int generate_contents(char *contents, char *key, char *value);

/* ------------------------------------------------
 * Global variables							
 * ------------------------------------------------*/
static CURL * g_curl = NULL;
static struct curl_slist *g_curl_headers = NULL;

/* ------------------------------------------------
 * Startup
 * ------------------------------------------------*/
//void _PG_init(void)
//{
////    curl_global_init(CURL_GLOBAL_ALL);
//}

/* ------------------------------------------------
 * Shutdown
 * ------------------------------------------------*/
//void _PG_fini(void)
//{
////    if ( g_curl )
////    {
////		curl_slist_free_all(g_curl_headers);
////        curl_easy_cleanup(g_curl);
////        g_curl = NULL;
////    }
////
////    curl_global_cleanup();
//}

/* ------------------------------------------------
 * pg_add_ingest_table().
 * 
 * Usage: select pg_add_ingest_table();
 * ------------------------------------------------ */
Datum
pg_add_ingest_table(PG_FUNCTION_ARGS)
{
	char *schema_name = text_to_cstring(PG_GETARG_TEXT_PP(0));
	char *table_name = text_to_cstring(PG_GETARG_TEXT_PP(1));
	int ndbtype  = PG_GETARG_INT32(2);
	int noperate = PG_GETARG_INT32(3);
	char *conn_info = text_to_cstring(PG_GETARG_TEXT_PP(4));
	custom_config_t config;

	char db_user[NAMEDATALEN];
	char db_name[NAMEDATALEN];
	char hostname[NAMEDATALEN];
	int ret = 0;

	/* Check parameters */

	/* check that the database type. */
	if (ndbtype != 1)
		DB_LOG_RETURN(ERROR, K_NOT_SUPPORT_DBTYPE, ret);

	/* check that the operating type. */
	if (noperate < 1 || noperate > 7)
		DB_LOG_RETURN(ERROR, K_CHECK_OPERATE, ret);

	CHECKMAXLEN(table_name, NAMEDATALEN);
	CHECKMAXLEN(schema_name , NAMEDATALEN);
	CHECKMAXLEN(conn_info, OPTLEN );

	/* check option.  to do ..*/
	if (conn_info == NULL && !strlen(conn_info))
		DB_LOG_RETURN(ERROR, K_CHECK_OPTION, ret);

	/* check that the table exists. */
	if((CHECK(ret, check_exists_table(schema_name, table_name))) < 0)
		DB_LOG_RETURN(ERROR, K_TABLE_NOT_EXIST, ret);

	/* Get custom config */
	if((CHECK(ret, get_custom_config(&config))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);
	
	/* Get database name and user id */
	if((CHECK(ret, get_database_info(db_user, db_name))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);
		
	/* update table name into bw_table_list */
	if((CHECK(ret, update_mapping_table(schema_name, table_name, true))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);

	/* check and run the bw process */
	if((CHECK(ret, control_process(db_name, db_user, hostname, conn_info, &config, MODE_INSERT))) < 0)
		DB_LOG_RETURN(ERROR, K_FAILED_START, ret);

	/* sync config with kafka connecta */
	if(strlen(config.consumer))
		if((CHECK(ret, sync_with_kafka_connect(db_name, NULL, conn_info, &config))) < 0)
			DB_LOG_RETURN(LOG, K_SUCCESS, ret);
	
	DB_LOG_RETURN(INFO, K_SUCCESS, ret);
}

/* ------------------------------------------------
 * pg_del_ingest_table().
 * 
 * Usage: select pg_del_ingest_table();
 * ------------------------------------------------
 */
Datum
pg_del_ingest_table(PG_FUNCTION_ARGS)
{
	char *schema_name = text_to_cstring(PG_GETARG_TEXT_PP(0));
	char *table_name = text_to_cstring(PG_GETARG_TEXT_PP(1));
	char db_user[NAMEDATALEN];
	char db_name[NAMEDATALEN];
	custom_config_t config;
	int ret = 0;

	CHECKMAXLEN(table_name, NAMEDATALEN);
	CHECKMAXLEN(schema_name, NAMEDATALEN);

	/* check that the table exists.*/
	if((CHECK(ret, check_exists_table(schema_name, table_name))) < 0)
		DB_LOG_RETURN(ERROR, K_TABLE_NOT_EXIST, ret);

	/* Get database name and user id */
	if((CHECK(ret, get_database_info(db_user, db_name))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);

	/* Get custom config */
	if((CHECK(ret, get_custom_config(&config))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);

	/* get config from kafka connect */
	if(strlen(config.consumer))
		if((CHECK(ret, sync_with_kafka_connect(db_name, table_name, NULL, &config))) < 0)
			DB_LOG_RETURN(ERROR, K_KAFKA_CONN_ERR, ret);

	/* update table name into bw_table_list */
	if((CHECK(ret, update_mapping_table(schema_name, table_name, false))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);

	/* check and send signal to reload conf */
	if((CHECK(ret, control_process(db_name, db_user, NULL, NULL, NULL, MODE_DELETE))) < 0)
		DB_LOG_RETURN(INFO, K_EVENT_FAIL, ret );

	DB_LOG_RETURN(INFO, K_SUCCESS, ret);
}

/* ------------------------------------------------
 * pg_resume_ingest().
 * 
 * Usage: select pg_resume_ingest();
 * ------------------------------------------------ */
Datum
pg_resume_ingest(PG_FUNCTION_ARGS)
{
	char db_user[NAMEDATALEN];
	char db_name[NAMEDATALEN];
	int ret;

	custom_config_t config;

	/* Get custom config */
	if((CHECK(ret, get_custom_config(&config))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);

	/* Get database name and user id */
	if((CHECK(ret, get_database_info(db_user, db_name))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);

	/* resume process */
	if((CHECK(ret, control_process(db_name, db_user, NULL, NULL, &config, MODE_RESUME))) < 0)
		DB_LOG_RETURN(ERROR, K_EVENT_FAIL,ret);

	DB_LOG_RETURN(INFO, K_SUCCESS, ret);
}

/* ------------------------------------------------
 * pg_suspend_ingest().
 * 
 * Usage: select pg_suspend_ingest();
 * ------------------------------------------------ */
Datum
pg_suspend_ingest(PG_FUNCTION_ARGS)
{
	char db_user[NAMEDATALEN];
	char db_name[NAMEDATALEN];
	int ret;

	/* Get database name and user id */
	if((CHECK(ret, get_database_info(db_user, db_name))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);

	/* suspend process and delete replication slot */
	if((CHECK(ret, control_process(db_name, db_user, NULL, NULL, NULL, MODE_SUSPEND))) < 0)
		DB_LOG_RETURN(ERROR, K_EVENT_FAIL, ret);

	if((CHECK(ret, remove_replication_slot(db_name))) < 0)
		DB_LOG_RETURN(ERROR, K_REMOVE_SLOT_ERR, ret);

	DB_LOG_RETURN(INFO, K_SUCCESS, ret);
}

/* ------------------------------------------------
 * pg_get_status_ingest().
 * 
 * Usage: select pg_get_status_ingest();
 * ------------------------------------------------ */
Datum
pg_get_status_ingest(PG_FUNCTION_ARGS)
{
	char db_user[NAMEDATALEN];
	char db_name[NAMEDATALEN];
	int ret = 0 ;

	/* Get database name and user id */
	if((CHECK(ret, get_database_info(db_user, db_name))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);

	/* Check process status */
	if (check_bw_process(db_name) > 0)
		DB_LOG_RETURN(INFO, K_PROC_WORKING, ret);

	/* Check replication  (to do..)*/
	/* Check Kafka Status (to do..)*/

	DB_LOG_RETURN(INFO, K_PROC_NOT_WORKING, ret);
}

/* ------------------------------------------------
 * pg_create_kafka_connect().
 * 
 * Usage: pg_create_kafka_connect('connect name');
 * ------------------------------------------------ */
Datum
pg_create_kafka_connect(PG_FUNCTION_ARGS)
{
	char contents[CONTENTSDATALEN];
	int ret = 0 ;
	char *conn_name = text_to_cstring(PG_GETARG_TEXT_PP(0));
	custom_config_t config;

	CHECKMAXLEN(conn_name, NAMEDATALEN);

	/* Get custom config */
	if((CHECK(ret, get_custom_config(&config))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);

	if(!strlen(config.consumer))
		DB_LOG_RETURN(ERROR, K_KAFKA_CONN_CONFIG, ECONFIG);

	if((CHECK(ret, get_kafka_connect_info(conn_name, contents))) >= 0)
		DB_LOG_RETURN(ERROR, K_EXIST , ret);
	/* Get default config of Kafka connect */
	if((CHECK(ret, http_get_kafka_connect(contents, &config))) < 0)
		DB_LOG_RETURN(ERROR, K_KAFKA_CONN_ERR, ret);

	/* Generate json data to request creation  */
	if((CHECK(ret, generate_contents(contents, "name", conn_name))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);

	/* Create Kafka connect by connect_name */
	if((CHECK(ret, http_create_kafka_connect(conn_name, contents, &config))) < 0)
		DB_LOG_RETURN(ERROR, K_KAFKA_CONN_ERR, ret);

	if((CHECK(ret, update_kafka_conn_info(MODE_INSERT, contents, conn_name))) < 0)
		DB_LOG_RETURN(INFO, K_SPI_ERR, ret);
	DB_LOG_RETURN(INFO, K_SUCCESS , ret);
}

/* ------------------------------------------------
 * pg_delete_kafka_connect().
 * 
 * Usage: pg_delete_kafka_connect('connect name');
 * ------------------------------------------------ */
Datum
pg_delete_kafka_connect(PG_FUNCTION_ARGS)
{
	char contents[CONTENTSDATALEN];
	int ret = 0 ;
	char *conn_name = text_to_cstring(PG_GETARG_TEXT_PP(0));
	custom_config_t config;

	CHECKMAXLEN(conn_name, NAMEDATALEN);

	/* Get custom config */
	if((CHECK(ret, get_custom_config(&config))) < 0)
		DB_LOG_RETURN(ERROR, K_SPI_ERR, ret);

	if(!strlen(config.consumer))
		DB_LOG_RETURN(ERROR, K_KAFKA_CONN_CONFIG, ECONFIG);

	/* Generate json data to request creation */
	if((CHECK(ret, get_kafka_connect_info(conn_name, contents))) < 0)
		DB_LOG_RETURN(ERROR, K_NOT_EXIST  , ret);

	/* Create Kafka connect by connect_name */
	if((CHECK(ret, http_delete_kafka_connect(conn_name, contents, &config))) < 0)
		DB_LOG_RETURN(ERROR, K_KAFKA_CONN_ERR, ret);

	if((CHECK(ret, update_kafka_conn_info(MODE_DELETE, contents, conn_name))) < 0)
		DB_LOG_RETURN(INFO, K_SPI_ERR, ret);
	DB_LOG_RETURN(INFO, K_SUCCESS , ret);
}

/* ----------------------------
 *  Check bottledwater process, 
 *  replication slot, kafka connect status
 * ---------------------------- */
int check_bw_process(const char* db_name)
{
	char tmppath[MAXPGPATH];	
	char pidbuf[32];
	FILE  *pidfp = NULL;
//	struct stat st;

	snprintf(tmppath, sizeof(tmppath)-1, "/tmp/bw_%s.pid", db_name);

	if ((pidfp = fopen(tmppath, "r")) == NULL){
		return 0; //process is not running
	}

	if (!fgets(pidbuf, sizeof(pidbuf), pidfp)){
		fclose(pidfp);
		return -1; //abnormal state - process started abnormally
	}
	
	fclose(pidfp);
	snprintf(tmppath, sizeof(tmppath), "/proc/%s/status", pidbuf);
	
	if((pidfp = fopen(tmppath, "r"))){
		fclose(pidfp);
		return atoi(pidbuf);
	}
	return -1;	
}

/* ----------------------------
 * Spawn bottledwater 
 * Send signal to reload table list
 * ---------------------------- */
int control_process(const char* db_name, const char* db_user, const char* hostname, const char* conn_info, const custom_config_t* config, int mode)
{
	int ret = 0;
	char conn_name[NAMEDATALEN];
	char command[MAXPGPATH];
	pid_t pid = check_bw_process(db_name);
	if(pid <= 0) {
		switch (mode){
		case MODE_INSERT:
		case MODE_RESUME:
			if(pid<=0){ //process is not running
				if(!strlen(config->consumer)){
					if(strlen(db_name) < NAMEDATALEN) strcpy(conn_name, db_name);}
				else{
					if((CHECK(ret, get_kafka_connect_info(conn_name, NULL))) < 0)
						if(strlen(db_name) < NAMEDATALEN) strcpy(conn_name, db_name);
				}

				snprintf(command, sizeof(command), 
						"nohup %s --postgres=postgres://%s@127.0.0.1/%s --slot=%s --broker=%s --schema-registry=%s --topic-prefix=%s --allow-unkeyed --on-error=log 1>/dev/null 2>&1 &",
						config->bwpath, db_user, db_name, db_name, config->broker, config->schema_registry, conn_name);
			//	snprintf(command, sizeof(command), "nohup /home/postgres/install/bottledwater-pg-master/kafka/bottledwater --postgres=postgres://localhost/protoavro --slot=protoavro --broker=K4M-KAFKA-1:9092 --schema-registry=http://K4M-KAFKA-1:8081 1>/dev/null/ 2>&1 &");
				CHECK_RETURN(ret, spawn_bw_process(command));
			}
			break;
		default:
			return EUKNOWN ;
		}
	}
	else {
		if(mode == MODE_SUSPEND)
			kill(pid, SIGTERM); //send signal to process - reload configuration 
		else{
			kill(pid, SIGQUIT); //send signal to process - reload configuration 
		}
	}
	return 0;
}

/*----------------------------
 * Check user input table name.
 *---------------------------- */
int	check_exists_table(const char * schema_name, const char * table_name)
{
	char sql[QBUFFLEN];
	int ret = 0;
    SPI_connect();
    snprintf(sql, sizeof(sql), CHECK_TABLE_EXISTS, schema_name, table_name);
    ret = SPI_exec(sql, 1);

    if (ret != SPI_OK_SELECT || SPI_tuptable == NULL || SPI_processed <= 0) {
		SPI_finish();
		return ESPI;
	}
    SPI_finish();
	return 0;
}

/*----------------------------
 * Add/Delete replication tables.
 *---------------------------- */
int update_mapping_table(const char * schema_name, const char * table_name, bool operation)
{
	char sql[QBUFFLEN];
	int i = 0;
	int ret = 0;
    SPI_connect();

	if(operation)
		snprintf(sql, QBUFFLEN-1, INSERT_MAP_TABLE, table_name, schema_name, schema_name, table_name, table_name, schema_name);
	else
		snprintf(sql, QBUFFLEN-1, DELETE_MAP_TABLE, schema_name, table_name);

    ret = SPI_exec(sql, 1);
	if(ret != SPI_OK_DELETE && ret != SPI_OK_INSERT){
		for(i = 0; i < 10; i++)
			ret = ESPI;
	}

	if(SPI_processed < 1){
		ret = ENEXIST ;
	}

    SPI_finish();
	return ret;
}

/*----------------------------
 * Remove replication slot
 *---------------------------- */
int remove_replication_slot(const char* db_name)
{
	char sql[QBUFFLEN];
	int ret = 0, i = 3;

	while ((check_bw_process(db_name) > 0) && i-- > 0){
		sleep(1);
	}

    SPI_connect();
    snprintf(sql, sizeof(sql), "select pg_drop_replication_slot('%s')", db_name);
    ret = SPI_exec(sql, 1);

    if (ret != SPI_OK_SELECT || SPI_tuptable == NULL || SPI_processed <= 0) {
		SPI_finish();
		return ESPI;
	}
	SPI_finish();
	return 0;
}

/*----------------------------
 * Get database info
 *---------------------------- */
int get_database_info(char *db_user, char *db_name)
{
	char sql[QBUFFLEN];
	int ret = 0;
	char *pdb_user;
	char *pdb_name;
    SPI_connect();
    snprintf(sql, sizeof(sql), GET_DATABASE_INFO);
    ret = SPI_exec(sql, 1);
	if (ret > 0 && SPI_tuptable != NULL && SPI_processed > 0){
		TupleDesc tupdesc = SPI_tuptable->tupdesc;
		SPITupleTable *tuptable = SPI_tuptable;

		HeapTuple tuple = tuptable->vals[0];

		pdb_user = SPI_getvalue(tuple, tupdesc, 1);
		pdb_name = SPI_getvalue(tuple, tupdesc, 2);
		if(strlen(pdb_user) < NAMEDATALEN) strcpy(db_user, pdb_user);
		if(strlen(pdb_name) < NAMEDATALEN) strcpy(db_name, pdb_name);
		xpfree(pdb_user);
		xpfree(pdb_name);
	}
	else {
		SPI_finish();
		return ESPI;
	}

    SPI_finish();
	return 0;
}

/*----------------------------
 * Get customized config from postgresql.auto.conf
 *---------------------------- */
int get_custom_config(custom_config_t *pconfig)
{
	int ret = 0;
	SPI_connect();
	ret = SPI_exec(GET_REPLICATION_SETTING, 1);
	if (ret > 0 && SPI_tuptable != NULL && SPI_processed > 0){
		TupleDesc tupdesc = SPI_tuptable->tupdesc;
		SPITupleTable *tuptable = SPI_tuptable;
		HeapTuple tuple = tuptable->vals[0];

		memset(pconfig, 0x00, sizeof(custom_config_t));

		strncpy(pconfig->bwpath, SPI_getvalue(tuple, tupdesc, 1), MAXPGPATH-1);
		strncpy(pconfig->broker, SPI_getvalue(tuple, tupdesc, 2), URLLEN-1);
		strncpy(pconfig->schema_registry, SPI_getvalue(tuple, tupdesc, 3), URLLEN-1);
		strncpy(pconfig->consumer, SPI_getvalue(tuple, tupdesc, 4), URLLEN-1);
		strncpy(pconfig->consumer_sub, SPI_getvalue(tuple, tupdesc, 5), URLLEN-1);

		SPI_finish();
	}
	else {
		SPI_finish();
		ret = ESPI;
	}
	return ret;
}

/*----------------------------
 * Get 'Kafka Connect' info
 * 1.connect name,
 * 2.configuration : HDFS interface, topics
 *---------------------------- */
int	get_kafka_connect_info(char *conn_name, char *contents)
{
	int ret = 0;

	char *pconn_name = NULL, *pcontents = NULL;
    SPI_connect();
    ret = SPI_exec(GET_KAFKA_CONN_INFO, 1);
    if ((ret == SPI_OK_SELECT) && (SPI_tuptable != NULL) && (SPI_processed > 0)) {
		TupleDesc tupdesc = SPI_tuptable->tupdesc;
		SPITupleTable *tuptable = SPI_tuptable;
		HeapTuple tuple = tuptable->vals[0];

		pconn_name= SPI_getvalue(tuple, tupdesc, 1);
		pcontents = SPI_getvalue(tuple, tupdesc, 2);
		if(conn_name)
			if(strlen(pconn_name) < NAMEDATALEN) strcpy(conn_name, pconn_name);
		if(contents)
			if(strlen(pcontents) < CONTENTSDATALEN) strcpy(contents, pcontents);
		xpfree(pconn_name);
		xpfree(pcontents);
	}
	else
		ret = ESPI;
	SPI_finish();
	return ret;
}

/*----------------------------
 * Set 'Kafka Connect' info
 * 1.connect name,
 * 2.configuration : HDFS interface, topics
 *---------------------------- */
int update_kafka_conn_info(unsigned char mode, const char *contents, const char *conn_name)
{
	int ret = 0;
	char sql[QBUFFLEN];
	SPI_connect();	
	if(mode == MODE_INSERT)
		snprintf(sql, sizeof(sql), INSERT_KAFKA_CONFIG, conn_name, contents);
	else if(mode == MODE_UPDATE)
		snprintf(sql, sizeof(sql), UPDATE_KAFKA_CONFIG, contents, conn_name);
	else
		snprintf(sql, sizeof(sql), DELETE_KAFKA_CONFIG);
	ret = SPI_exec(sql, 1);
	SPI_finish();
	if (ret != SPI_OK_INSERT && ret != SPI_OK_UPDATE && ret != SPI_OK_DELETE)
		ret = ESPI;
	return ret;
}

/*----------------------------
 * Get mapping table list to make json message. 
 *---------------------------- */
int get_topic_list(const char* conn_name, const char* table_name, char **topics)
{
	int ret = 0, i, proc = 0, nsize = 0, offset = 0, namelen = 0;
	char *ptopics;
	char sql[QBUFFLEN];
	if(conn_name)
		namelen = strlen(conn_name);
    ret = SPI_connect();
	if( table_name)
		snprintf(sql, sizeof(sql), GET_TOPIC_LIST_EXCEPT_PARAM , table_name);
	else
		snprintf(sql, sizeof(sql), GET_TOPIC_LIST);
    ret = SPI_exec(sql, 0);
	proc = SPI_processed;
    if (ret == SPI_OK_SELECT && SPI_tuptable != NULL && SPI_processed >= 0) {
		if (proc > 0) {
			TupleDesc tupdesc = SPI_tuptable->tupdesc;
			SPITupleTable *tuptable = SPI_tuptable;
			HeapTuple tuple;

			for(i = 0; i < proc; i++){
				tuple = tuptable->vals[i];
				ptopics = SPI_getvalue(tuple, tupdesc, 1);
				nsize += strlen(ptopics)+namelen+2+1;//conn_name length+','+'.'+NULL,
			}

			*topics = (char*)malloc(nsize);
			memset(*topics, 0x00, nsize);
			for(i = 0; i < proc; i++){
				tuple = tuptable->vals[i];
				ptopics = SPI_getvalue(tuple, tupdesc, 1);
				offset  += snprintf(*topics+offset, nsize, "%s.%s,", conn_name, ptopics);
				xpfree(ptopics);
			}
		}
		else
			ret = 0;
	}
	else
		ret = -1;
	SPI_finish();
	return ret;
}

/*----------------------------
 * Make json message to update config of K.C
 *---------------------------- */
int generate_contents(char *contents, char *key, char *value)
{
	char sql[QBUFFLEN];
	int ret = 0, len = 0;
	char * pcontents = NULL;
    SPI_connect();
	if(!strcmp(key, "topics"))
		snprintf(sql, sizeof(sql), GENERATE_TOPICS, contents, value ? value : "");
	else if(!strcmp(key, "name"))
		snprintf(sql, sizeof(sql), GENERATE_CONNECT_NAME, value, contents, value);

	//elog(INFO, "<<<%s>>>",sql);
    ret = SPI_exec(sql, 1);
    if (ret == SPI_OK_SELECT && SPI_tuptable != NULL && SPI_processed > 0) {
		TupleDesc tupdesc = SPI_tuptable->tupdesc;
		SPITupleTable *tuptable = SPI_tuptable;
		HeapTuple tuple;

		tuple = tuptable->vals[0];
		pcontents = SPI_getvalue(tuple, tupdesc, 1);

		len = strlen(pcontents) ;
		if(len >= CONTENTSDATALEN)
			ret = ELEN;
		else {
			if(strlen(pcontents) < CONTENTSDATALEN) strcpy(contents, pcontents);
		}
		xpfree(pcontents);
	}
	else
		ret = -1;
	SPI_finish();
	return ret;
}

/*-----------------------------
* Response callback by CURL lib
* Registered CURLOPT_WRITEFUNCTION
*----------------------------- */
static size_t response_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;

	mem->memory = realloc(mem->memory, mem->size + realsize + 1);
	if(mem->memory == NULL) {
		/* out of memory! */ 
		printf("not enough memory (realloc returned NULL)\n");
		return 0;
	}

	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;
	return realsize;
}

/*----------------------------
 * Spawn a process to execute the given shell command; don't wait for it
 * Returns the process ID (or HANDLE) so we can wait for it later
 *---------------------------- */
pid_t spawn_bw_process(const char *cmdline)
{
	pid_t		pid;

	/*
	 * Must flush I/O buffers before fork.  Ideally we'd use fflush(NULL) here
	 * ... does anyone still care about systems where that doesn't work?
	 */
	fflush(stdout);
	fflush(stderr);

	pid = fork();
	if (pid == -1)
	{
//		fprintf(stderr, _("%s: could not fork: %s\n"), cmdline, strerror(errno));
//		exit(2);
		return ERUN;
	}
	if (pid == 0)
	{
		/*
		 * In child
		 *
		 * Instead of using system(), exec the shell directly, and tell it to
		 * "exec" the command too.  This saves two useless processes per
		 * parallel test case.
		 */
		char	   *cmdline2;

		cmdline2 = psprintf("exec %s", cmdline);
		execl("/bin/sh", "/bin/sh", "-c", cmdline2, (char *) NULL);
//		fprintf(stderr, _("%s: could not exec \"%s\": %s\n"), progname, shellprog, strerror(errno));
		_exit(1);				/* not exit() here... */
	}
	/* in parent */
	return pid;
}

/*----------------------------
 * Sycronize topics and other config with K.C
 *---------------------------- */
int sync_with_kafka_connect(const char* db_name, const char* table_name, const char* conn_info, const custom_config_t* config)
{
	char contents[CONTENTSDATALEN], conn_name[NAMEDATALEN], *topics = NULL;
	int ret = 0;

	CHECK_RETURN(ret, get_kafka_connect_info(conn_name, contents));
	CHECK_RETURN(ret, get_topic_list(conn_name, table_name, &topics));
	CHECK_RETURN(ret, generate_contents(contents, "topics", topics));
	CHECK_RETURN(ret, http_set_kafka_connect(conn_name, contents, config));
	CHECK_RETURN(ret, update_kafka_conn_info(MODE_UPDATE, contents, conn_name));

	xfree(topics);
	return ret;
}

/*----------------------------
 * Get Request : default config 
 *---------------------------- */
int http_get_kafka_connect(char *contents, const custom_config_t *config)
{
	char curl_error[CURL_ERROR_SIZE];
	struct MemoryStruct response;
	int ret = 0;
	long http_code = 0;
	char url[URLLEN];

    curl_global_init(CURL_GLOBAL_ALL);
	g_curl = curl_easy_init();
	g_curl_headers = curl_slist_append(NULL, "Content-Type: " CONTENT_TYPE);
	g_curl_headers = curl_slist_append(g_curl_headers, "Accept: " CONTENT_TYPE);

	response.memory = malloc(2);
	response.size = 0;

	snprintf(url, URLLEN-1, "%s/%s/", config->consumer, config->consumer_sub);
	curl_easy_setopt(g_curl, CURLOPT_URL, url);
	curl_easy_setopt(g_curl, CURLOPT_HTTPHEADER, g_curl_headers);
	curl_easy_setopt(g_curl, CURLOPT_WRITEFUNCTION, response_cb);
	curl_easy_setopt(g_curl, CURLOPT_WRITEDATA, (void*)&response);
	curl_easy_setopt(g_curl, CURLOPT_ERRORBUFFER, curl_error);
	curl_easy_setopt(g_curl, CURLOPT_TIMEOUT, HTTP_TIMOUT);

	if(curl_easy_perform(g_curl) != CURLE_OK){
		ret = EHTTP;
		xfree(response.memory);
		curl_slist_free_all(g_curl_headers);
		curl_easy_cleanup(g_curl);
		g_curl_headers = NULL;
        g_curl = NULL;
        curl_global_cleanup();
		return ret;
	}
	if(strlen(response.memory) < CONTENTSDATALEN)
		strcpy(contents, response.memory);

	if(!contents){
		ret = EMEM;
		xfree(response.memory);
		curl_slist_free_all(g_curl_headers);
		curl_easy_cleanup(g_curl);
		g_curl_headers = NULL;
        g_curl = NULL;
        curl_global_cleanup();
		return ret;
	}

	curl_easy_getinfo(g_curl, CURLINFO_RESPONSE_CODE, &http_code);
	if(http_code != 200){
		ret = EHTTP;
	}

	xfree(response.memory);
	curl_slist_free_all(g_curl_headers);
	curl_easy_cleanup(g_curl);
	g_curl_headers = NULL;
    g_curl = NULL;
    curl_global_cleanup();
	return ret;
}

/*----------------------------
 * PUT Request:Send config to K.C 
 *---------------------------- */
int http_set_kafka_connect(const char* conn_name, const char *contents, const custom_config_t *config)
{
	char curl_error[CURL_ERROR_SIZE];
	char url[URLLEN];
	int ret = 0;
	long http_code = 0;

    curl_global_init(CURL_GLOBAL_ALL);
	g_curl = curl_easy_init();
	g_curl_headers = curl_slist_append(NULL, "Content-Type: " CONTENT_TYPE);
	g_curl_headers = curl_slist_append(g_curl_headers, "Accept: " CONTENT_TYPE);

	snprintf(url, URLLEN-1, "%s/%s/config", config->consumer, conn_name);
	curl_easy_setopt(g_curl, CURLOPT_URL, url);
	curl_easy_setopt(g_curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(g_curl, CURLOPT_POSTFIELDS, contents);
	curl_easy_setopt(g_curl, CURLOPT_HTTPHEADER, g_curl_headers);
	curl_easy_setopt(g_curl, CURLOPT_ERRORBUFFER, curl_error);
	curl_easy_setopt(g_curl, CURLOPT_TIMEOUT, HTTP_TIMOUT);

	if(curl_easy_perform(g_curl) != CURLE_OK){
		ret = EHTTP;
		curl_slist_free_all(g_curl_headers);
		curl_easy_cleanup(g_curl);
		g_curl_headers = NULL;
        g_curl = NULL;
        curl_global_cleanup();
		return ret;
	}

	curl_easy_getinfo(g_curl, CURLINFO_RESPONSE_CODE, &http_code);
	if(http_code != 200){
		ret = EHTTP;
	}

	curl_slist_free_all(g_curl_headers);
	curl_easy_cleanup(g_curl);
	g_curl_headers = NULL;
    g_curl = NULL;
    curl_global_cleanup();
	return ret;
}

/*----------------------------
 * POST : Create instance request to K.C
 *---------------------------- */
int http_create_kafka_connect(const char *conn_name, const char *contents, const custom_config_t *config)
{
	char curl_error[CURL_ERROR_SIZE];
	int ret = 0;
	long http_code = 0;

    curl_global_init(CURL_GLOBAL_ALL);
	g_curl = curl_easy_init();
	g_curl_headers = curl_slist_append(NULL, "Content-Type: " CONTENT_TYPE);
	g_curl_headers = curl_slist_append(g_curl_headers, "Accept: " CONTENT_TYPE);

	curl_easy_setopt(g_curl, CURLOPT_URL, config->consumer);
	curl_easy_setopt(g_curl, CURLOPT_HTTPHEADER, g_curl_headers);
    curl_easy_setopt(g_curl, CURLOPT_POSTFIELDS, contents);
	curl_easy_setopt(g_curl, CURLOPT_ERRORBUFFER, curl_error);
	curl_easy_setopt(g_curl, CURLOPT_TIMEOUT, HTTP_TIMOUT);

	if(curl_easy_perform(g_curl) != CURLE_OK){
		ret = EHTTP;
		curl_slist_free_all(g_curl_headers);
		curl_easy_cleanup(g_curl);
		g_curl_headers = NULL;
        g_curl = NULL;
        curl_global_cleanup();
		return ret;
	}

	curl_easy_getinfo(g_curl, CURLINFO_RESPONSE_CODE, &http_code);
	if(http_code != 201){
		ret = EHTTP;
	}

	curl_slist_free_all(g_curl_headers);
	curl_easy_cleanup(g_curl);
	g_curl_headers = NULL;
    g_curl = NULL;
    curl_global_cleanup();
	return ret;
}

/*----------------------------
 * DELETE Request : delete instance of K.C
 *---------------------------- */
int http_delete_kafka_connect(const char *conn_name, const char *contents, const custom_config_t *config)
{
	char curl_error[CURL_ERROR_SIZE];
	int ret = 0;
	long http_code = 0;
	char url[URLLEN];

    curl_global_init(CURL_GLOBAL_ALL);
	g_curl = curl_easy_init();
	g_curl_headers = curl_slist_append(NULL, "Content-Type: " CONTENT_TYPE);
	g_curl_headers = curl_slist_append(g_curl_headers, "Accept: " CONTENT_TYPE);

	snprintf(url, URLLEN-1, "%s/%s", config->consumer, conn_name);
	curl_easy_setopt(g_curl, CURLOPT_URL, url);
	curl_easy_setopt(g_curl, CURLOPT_HTTPHEADER, g_curl_headers);
	curl_easy_setopt(g_curl, CURLOPT_CUSTOMREQUEST, "DELETE");
	curl_easy_setopt(g_curl, CURLOPT_ERRORBUFFER, curl_error);
	curl_easy_setopt(g_curl, CURLOPT_TIMEOUT, HTTP_TIMOUT);

	if(curl_easy_perform(g_curl) != CURLE_OK){
		ret = EHTTP;
		curl_slist_free_all(g_curl_headers);
		curl_easy_cleanup(g_curl);
		g_curl_headers = NULL;
        g_curl = NULL;
        curl_global_cleanup();
		return ret;
	}

	curl_easy_getinfo(g_curl, CURLINFO_RESPONSE_CODE, &http_code);
	if(http_code != 204){
		ret = EHTTP;
	}

	curl_slist_free_all(g_curl_headers);
	curl_easy_cleanup(g_curl);
	g_curl_headers = NULL;
    g_curl = NULL;
    curl_global_cleanup();
	return ret;
}
