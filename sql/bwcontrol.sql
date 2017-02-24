CREATE EXTENSION bwcontrol;

--
-- 
-- 
-- 
--

select pg_make_kafka_connect('connect-name'); -- 
create table test (a int primary key, b int[]);
select pg_add_ingest_table('myschema', 'test', 1, 1, '');
select pg_get_status_ingest();
select pg_suspend_ingest();
select pg_resume_ingest();
select pg_del_ingest_table('myschema', 'test');
select pg_delete_kafka_connect('connect-name');
select pg_suspend_ingest();
