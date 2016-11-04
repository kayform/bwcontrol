#ifndef _ERROR_STRING_H_

#define _ERROR_STRING_H_

#define K_TABLE_NOT_EXIST "No such table."
#define K_NOT_SUPPORT_DBTYPE "Not support db type.(1 or 2)"
#define K_CHECK_OPERATE "Check operate type. (operate 1~7)"
#define K_CHECK_OPTION "Could not parse option."

#define K_EXIST "Already exists."
#define K_NOT_EXIST "Could not find data."
#define K_REMOVE_SLOT_ERR "Could not remove repl slot"
#define K_SPI_ERR "Could not get relevant result from database"
#define K_FAILED_START "Could not start to repl"
#define K_EVENT_FAIL "Could not send signal to process"
#define K_KAFKA_CONN_ERR "Could not connect to kafka connect"
#define K_KAFKA_CONN_CONFIG "Check kafka connect URL"

#define K_UNKNOWN_ERROR "Unknown error"
#define K_INTERNAL_ERROR "internal error"

#define K_SUCCESS "Success"

#define K_PROC_NOT_WORKING "Process stopped"
#define K_PROC_WORKING "Process is running"

/* Error define */
#define ESPI (-8)
#define ERUN (-7)
#define ELEN (-6)
#define EMEM (-5)
#define ECONFIG (-4)
#define ENEXIST (-3)
#define EHTTP (-2)
#define EUKNOWN (-1)

#define ECONSUMER (10)

#endif /* _ERROR_STRING_H_ */
