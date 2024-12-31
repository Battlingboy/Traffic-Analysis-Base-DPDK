#ifndef MYSQL_OP_H
#define MYSQL_OP_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include <mysql/mysql.h>

#include "generate_query.h"
#include "flow_extractor.h"


#define MYSQL_HOST "127.0.0.1"          // 连接MySQL数据库的主机地址
#define MYSQL_USER "【mysql_username】"              // 连接MySQL数据库的用户名
#define MYSQL_PASS "【mysql_password】"         // 连接MySQL数据库的密码
#define MYSQL_DB "【mysql_dbname】"     // 连接MySQL数据库的数据库名
#define MYSQL_TABLE_NAME "【mysql_tablename】"   // MySQL数据库的表名

#define INSERT_BATCH_SIZE 1024 * 2      // MySQL批量插入的数据量

int collect_mysql(MYSQL *mysql_conn, const char *mysql_host, const char *mysql_user, const char *mysql_pass, const char *mysql_db);
int create_table(MYSQL *mysql_conn, const char *table_name);
int deal_result(MYSQL *mysql_conn, MYSQL_RES *result);
int show_tables(MYSQL *mysql_conn);
int insert_data(MYSQL *mysql_conn, const char *table_name, const struct IPv4FlowFeature flow_features[], int flow_num);
int insert_data_direct(MYSQL *mysql_conn, const char *table_name, const struct IPv4FlowFeature flow_features[], int flow_num);
int load_file_into_mysql(MYSQL *mysql_conn, const char *table_name, const char *file_name, int pdu_num);

#endif
