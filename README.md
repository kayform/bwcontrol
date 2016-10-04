# bwcontrol

Introduction
============

**bwcontrol** is an control plugin for bottled water. It means that the plugin can control Bottledwater process by APIs and this plugin support configurable whitelist of table feature. Also, have communication interface with Kafka-Connect with JSON format via HTTP.

**bwcontrol** is released under PostgreSQL license.

Requirements
============

* PostgreSQL 9.5+
* Bottledwater (custmized version: https://github.com/kayform/bottledwater-pg.git)

For addtional interworking
Refer to https://github.com/confluentinc

* Kafka Broker
* Schema registry
* zookeeper
* Kafka-Connect

Build and Install
=================

This extension is supported on [those platforms](http://www.postgresql.org/docs/current/static/supported-platforms.html) that PostgreSQL is. The installation steps depend on your operating system.

You can also keep up with the latest fixes and features cloning the Git repository.

```
$ git clone https://github.com/kayform/bwcontrol.git
```

Unix based Operating Systems
----------------------------

Before use this extension, you should build it and load it at the desirable database.

```
$ git clone https://github.com/kayform/bwcontrol.git
$ PATH=/path/to/bin/pg_config:$PATH
$ USE_PGXS=1 make
$ USE_PGXS=1 make install
```

Configuration
=============

You need to add some  parameters at postgresql.auto.conf:

```
bw.bwpath = '/path/to/bin/bottledwater'
bw.kafka_broker = 'KAFKA:9092'
bw.schema_registry='KAFKA:8081'
bw.consumer='KAFKA:8083/connectors'
```

After changing these parameters, a restart or reload using 'pg_reload_conf()' is needed.

Examples
========

calling functions via SQL.
1. Create connection with Kafka-connect to synchronize whithlisting of table.
	testdb=# pg_create_kafka_connect("testconnection");
	INFO:  Success(0)
2. Make whitelisting of table.
	testdb=# pg_add_ingest_table("testtable", 1, 1, '');
	INFO:  Success(0)
3. Remove table from whitelist to except sync with Kafka-connect.
	testdb=# pg_del_ingest_table("testtable");
	INFO:  Success(0)
4. Check status (bwttledwater, replication_slot, kafka connect, whitelist).
	testdb=# select pg_get_status_ingest();
	INFO:  process running(0)
5. Drop connection with Kafka-connect. It will stop to synchronize data stream with Kafka.
	testdb=# pg_delete_kafka_connect("testconnection");
	INFO:  Success(0)


 -----------------------------|									|--------|	|---------|
 |                            |    								|        |  |         |
 |  PostgreSQL                |                                 |        |  |         |
 |                            |                                 | Kafka  |  | Kafka   |
 |		 ---------------------|  Run/Stop   |---------------|   | broker |  | Consumer|
 |		 |                    | <---------->|               |   |        |  |         |
 |       | bwcontrol plugin   |  Whitelist  | bottledwater  |   |        |  |         |
 |       |                    | <---------->|               |   |        |  |         |
 |		 ---------------------|			    |               |   |        |  |         |
 |       |                    | Change Data |               |   |		 |  |		  |
 |       | bottledwater plugin|  Streaming  |               |   |        |  |         |
 |       |                    | ----------> |               |   |        |  |         |
 |-----------------------------	 		    -----------------   ----------  -----------



Bottledwater
--------------

Besides the configuration above, it is necessary to configure a replication connection to use bottledwater

First, add an entry at pg_hba.conf:

```
local    replication     myuser                     trust
host     replication     myuser     10.1.2.3/32     trust
```

Also, set max_wal_senders at postgresql.conf:

```
max_wal_senders = 1
```

A restart is necessary if you changed max_wal_senders.

You are ready to try bwcontrol. In one terminal:

```
```

In another terminal: kafka consol consumer

```
```

The output in the terminal is: kafka consol consumer

```
```

Dropping ...

```
```

SQL functions
-------------

pg_add_ingest_table(text table_name, int bigdata_type, int operation_type, text remark);
pg_del_ingest_table(text table_name)
pg_resume_ingest()
pg_suspend_ingest()
pg_get_status_ingest()
pg_create_kafka_connect(text connect_name)
pg_delete_kafka_connect(text connect_name)

License
=======

> bwcontrol is released under the PostgreSQL License, a liberal Open Source license, similar to the BSD or MIT licenses.

> Copyright 2016 K4M, Inc. All rights reserved.

> Permission to use, copy, modify, and distribute this software and its documentation for any purpose, without fee, and without a written agreement is hereby granted, provided that the above copyright notice and this paragraph and the following two paragraphs appear in all copies.

> IN NO EVENT SHALL THE AUTHOR BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

> THE AUTHOR SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE AUTHOR HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

>
