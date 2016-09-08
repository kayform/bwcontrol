# contrib/bwcontrol/Makefile

MODULE_big	= bwcontrol
OBJS = bwcontrol.o
EXTENSION = bwcontrol
DATA = bwcontrol--1.0.sql
PGFILEDESC = "bwcontrol - bottledwater control"

REGRESS = bwcontrol

#USE_PGXS

## if using curl to make session with kafka via out of channel"
CURL_CONFIG = curl-config
#JSON_CFLAGS = $(shell pkg-config --cflags jansson)
#JSON_LDFLAGS = $(shell pkg-config --libs jansson)
CFLAGS += $(shell $(CURL_CONFIG) --cflags)
#CFLAGS += $(shell $(JSON_CONFIG) --cflags)
LIBS += $(shell $(CURL_CONFIG) --libs)
SHLIB_LINK := $(LIBS)
## else
#CFLAGS += -g -ggdb
PG_CFLAGS += -std=c99 -g -ggdb
PG_CPPFLAGS += -std=c99 -g -ggdb
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
