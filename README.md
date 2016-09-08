# bwcontrol

Introduction
============

**bwcontrol** is an control plugin for bottled water. It means that the plugin can control Bottledwater process by APIs. Also, have communication interface with Kafka-Connect with JSON format via HTTP.

**bwcontrol** is released under PostgreSQL license.

Requirements
============

* PostgreSQL 9.4+

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
pg....

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

```
$ cat /tmp/example2.sql
```

The script above produces the output below:

```
```

License
=======

> Copyright 2016 K4M, Inc.
> All rights reserved.

> Licensed released under tht PostgreSQL license.
> Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

> Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

> Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

> Neither the name of the {organization} nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

>
