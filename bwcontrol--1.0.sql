/* contrib/bwcontrol/bwcontrol--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION bwcontrol" to load this file. \quit

-- Create functions
CREATE FUNCTION pg_add_ingest_table(text, text, INT, INT, text, INT)
RETURNS text
AS 'MODULE_PATHNAME', 'pg_add_ingest_table'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_del_ingest_table(text, text)
RETURNS text
AS 'MODULE_PATHNAME', 'pg_del_ingest_table'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_add_ingest_column(text, text, text, text)
RETURNS text
AS 'MODULE_PATHNAME', 'pg_add_ingest_column'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_del_ingest_column(text, text, text)
RETURNS text
AS 'MODULE_PATHNAME', 'pg_del_ingest_column'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_resume_ingest(INT)
RETURNS text
AS 'MODULE_PATHNAME', 'pg_resume_ingest'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_suspend_ingest()
RETURNS text
AS 'MODULE_PATHNAME', 'pg_suspend_ingest'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_get_status_ingest()
RETURNS text
AS 'MODULE_PATHNAME', 'pg_get_status_ingest'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_make_kafka_connect(text)
RETURNS text
AS 'MODULE_PATHNAME', 'pg_make_kafka_connect'
LANGUAGE C IMMUTABLE;

CREATE FUNCTION pg_delete_kafka_connect(text)
RETURNS text
AS 'MODULE_PATHNAME', 'pg_delete_kafka_connect'
LANGUAGE C IMMUTABLE;

-- Create metadata tables
-- CREATE TABLE tbl_mapps
-- (
-- 	database_name varchar(100) not null,
-- 	table_schema varchar(100) not null,
-- 	table_name varchar(100) not null,
-- 	relnamespace oid not null,
-- 	reloid oid not null,
-- 	topic_name varchar(100) not null,
-- 	create_date timestamp not null default now(),
-- 	create_user varchar(100) not null default current_user,
-- 	remark varchar(1000)
-- );
-- 
-- ALTER TABLE tbl_mapps ADD CONSTRAINT pk_tbl_mapps PRIMARY KEY(database_name,  table_schema,table_name);
-- 
-- COMMENT ON COLUMN tbl_mapps.database_name is 'DATABASE명';
-- COMMENT ON COLUMN tbl_mapps.table_schema is '테이블 스키마명';
-- COMMENT ON COLUMN tbl_mapps.table_name is '테이블명';
-- COMMENT ON COLUMN tbl_mapps.relnamespace is '테이블 스키마명 OID';
-- COMMENT ON COLUMN tbl_mapps.reloid is '테이블 OID';
-- COMMENT ON COLUMN tbl_mapps.topic_name is 'KAFKA TOPIC 명';
-- COMMENT ON COLUMN tbl_mapps.create_date is '등록일자';
-- COMMENT ON COLUMN tbl_mapps.create_user is '등록사용자';
-- COMMENT ON COLUMN tbl_mapps.remark is '비고';
-- 
-- CREATE TABLE tbl_mapps_hist
-- (
-- 	database_name varchar(100) not null,
-- 	table_schema varchar(100) not null,
-- 	table_name varchar(100) not null,
-- 	mod_date timestamp not null default now(),
-- 	mod_user varchar(100) not null default current_user,
-- 	mod_type varchar(100) not null,
-- 	relnamespace oid not null,
-- 	reloid oid not null,
-- 	topic_name varchar(100) not null,
-- 	create_date timestamp not null,
-- 	create_user varchar(100) not null,
-- 	remark varchar(1000)
-- );
-- 
-- ALTER TABLE tbl_mapps_hist ADD CONSTRAINT pk_tbl_mapps_hist PRIMARY KEY(database_name,  table_schema,table_name, mod_date);
-- 
-- COMMENT ON COLUMN tbl_mapps_hist.database_name is 'DATABASE명';
-- COMMENT ON COLUMN tbl_mapps_hist.table_schema is '테이블 스키마명';
-- COMMENT ON COLUMN tbl_mapps_hist.table_name is '테이블명';
-- COMMENT ON COLUMN tbl_mapps_hist.mod_date is '이력날짜';
-- COMMENT ON COLUMN tbl_mapps_hist.mod_user is '이력수행유저';
-- COMMENT ON COLUMN tbl_mapps_hist.mod_type is '이력타입';
-- COMMENT ON COLUMN tbl_mapps_hist.relnamespace is '테이블 스키마명 OID';
-- COMMENT ON COLUMN tbl_mapps_hist.reloid is '테이블 OID';
-- COMMENT ON COLUMN tbl_mapps_hist.topic_name is 'KAFKA TOPIC 명';
-- COMMENT ON COLUMN tbl_mapps_hist.create_date is '등록일자';
-- COMMENT ON COLUMN tbl_mapps_hist.create_user is '등록사용자';
-- COMMENT ON COLUMN tbl_mapps_hist.remark is '비고';
-- 
-- CREATE TABLE kafka_con_config
-- (
-- 	database_name varchar(100) not null,
-- 	connect_name varchar(100) not null,
-- 	contents json not null,
-- 	create_date timestamp not null,
-- 	create_user varchar(100) not null,
-- 	remark varchar(1000)
-- );
-- 
-- ALTER TABLE kafka_con_config ADD constraint pk_kafka_con_config PRIMARY KEY(database_name, connect_name);
-- 
-- COMMENT ON COLUMN kafka_con_config.database_name is 'DATABASE명';
-- COMMENT ON COLUMN kafka_con_config.connect_name is 'kafka-connect 이름';
-- COMMENT ON COLUMN kafka_con_config.contents is 'configuration';
-- COMMENT ON COLUMN kafka_con_config.create_date is '등록일자';
-- COMMENT ON COLUMN kafka_con_config.create_user is '등록사용자';
-- COMMENT ON COLUMN kafka_con_config.remark is '비고';

