#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <dirent.h>
#include <pthread.h>

#include <rte_ring.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_mbuf.h>
#include <rte_branch_prediction.h>
#include <rte_version.h>
#include <rte_malloc.h>

#include "lzo/lzowrite.h"
#include "pcap.h"
#include "utils.h"

#include "core_write.h"
#include "flow_extractor.h"
#include "mysql_op.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define RTE_LOGTYPE_DPDKCAP RTE_LOGTYPE_USER1

// static struct IPv4FlowFeature ipv4_flow_features[DEFAULT_NUM_IPV4_FLOWS];
// static struct IPv6FlowFeature ipv6_flow_features[DEFAULT_NUM_IPV6_FLOWS];

/*
 * Change file name from template
 */
static void format_from_template(
    char *filename,
    const char *template,
    const int core_id,
    const int file_count,
    const struct timeval *file_start)
{
    char str_buf[DPDKCAP_OUTPUT_FILENAME_LENGTH];
    // Change file name
    strncpy(filename, template, DPDKCAP_OUTPUT_FILENAME_LENGTH);
    snprintf(str_buf, 50, "%02d", core_id);
    while (str_replace(filename, "\%COREID", str_buf));
    snprintf(str_buf, 50, "%03d", file_count);
    while (str_replace(filename, "\%FCOUNT", str_buf));
    strncpy(str_buf, filename, DPDKCAP_OUTPUT_FILENAME_LENGTH);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    strftime(filename, DPDKCAP_OUTPUT_FILENAME_LENGTH, str_buf, localtime(&(file_start->tv_sec)));
#pragma GCC diagnostic pop
}

/*
 * Open pcap file for writing
 */
static FILE *open_pcap(char *output_file)
{
    FILE *file;
    // Open file
    file = fopen(output_file, "w");
    if (unlikely(!file))
    {
        RTE_LOG(ERR, DPDKCAP, "Core %d could not open %s in write mode: %d (%s)\n",
                rte_lcore_id(), output_file, errno, strerror(errno));
    }

    return file;
}

/*
 * Write into a pcap file
 */
static int write_pcap(FILE *file, void *src, size_t len)
{
    size_t retval;
    // Write file
    retval = fwrite(src, 1, len, file);
    if (unlikely(retval < 0))
    {
        RTE_LOG(ERR, DPDKCAP, "Could not write into file: %d (%s)\n", errno, strerror(errno));
        return -1;
    }
    return retval;
}

/*
 * Close and free a pcap file
 */
static int close_pcap(FILE *file)
{
    int retval;
    // Close file
    retval = fclose(file);
    if (unlikely(retval))
    {
        RTE_LOG(ERR, DPDKCAP, "Could not close file: %d (%s)\n",
                errno, strerror(errno));
    }
    return retval;
}

/*
 * Allocates a new lzowrite_buffer from the given file
 */
static struct lzowrite_buffer *open_lzo_pcap(char *output_file)
{
    struct lzowrite_buffer *buffer;
    FILE *file;

    // Open file
    file = fopen(output_file, "w");
    if (unlikely(!file))
    {
        RTE_LOG(ERR, DPDKCAP, "Core %d could not open %s in write mode: %d (%s)\n",
                rte_lcore_id(), output_file, errno, strerror(errno));
        goto cleanup;
    }

    // Init lzo file
    buffer = lzowrite_init(file);
    if (unlikely(!buffer))
    {
        RTE_LOG(ERR, DPDKCAP, "Core %d could not init lzo in file: %s\n",
                rte_lcore_id(), output_file);
        goto cleanup_file;
    }

    return buffer;
cleanup_file:
    fclose(file);
cleanup:
    return NULL;
}

/*
 * Free a lzowrite_buffer
 */
static int close_lzo_pcap(struct lzowrite_buffer *buffer)
{
    FILE *file = buffer->output;
    int retval;

    /* Closes the lzo buffer */
    retval = lzowrite_close(buffer);
    if (unlikely(retval))
    {
        RTE_LOG(ERR, DPDKCAP, "Could not close lzowrite_buffer.\n");
        return retval;
    }

    /* Close file */
    retval = fclose(file);
    if (unlikely(retval))
    {
        RTE_LOG(ERR, DPDKCAP, "Could not close file: %d (%s)\n",
                errno, strerror(errno));
        return retval;
    }

    return 0;
}

/*
 * 将流特征写入csv文件，并导入MySQL数据库
 */
void write_upload_flow(struct WriteUploadIpv4Flow *write_upload_ipv4_flow) {
    /* 将flow表中的特征写入文件 */
    char csv_file_name[DPDKCAP_OUTPUT_FILENAME_LENGTH] = {0};
    FILE *fp;
    sprintf(csv_file_name, "./csv/output_ipv4_%d_%d.csv", write_upload_ipv4_flow->core_id, write_upload_ipv4_flow->table_id);
    fp = fopen(csv_file_name, "w");
    if (unlikely(!fp)) {
        RTE_LOG(ERR, DPDKCAP, "Core %d could not open %s in write mode: %d (%s)\n",
                rte_lcore_id(), csv_file_name, errno, strerror(errno));
    }
    // printf("写入 IPv4 特征文件: %s\n", csv_file_name);
    write_ipv4_flow_features_to_csv(write_upload_ipv4_flow->ipv4_flow_features, DEFAULT_NUM_IPV4_FLOWS, fp);
    fclose(fp);

    uint64_t mysql_start_time = rte_rdtsc();
    /* 获取 csv文件的绝对路径 */
    char csv_file_abpath[DPDKCAP_OUTPUT_FILENAME_LENGTH] = {0};
    realpath(csv_file_name, csv_file_abpath);
    // printf("开始插入数据库: %s\n", csv_file_name);
    load_file_into_mysql(write_upload_ipv4_flow->mysql_conn, MYSQL_TABLE_NAME, csv_file_abpath, DEFAULT_FLOW_SEQ_LEN);
    uint64_t msyql_duration = (rte_rdtsc() - mysql_start_time) * 1000 / rte_get_timer_hz();
    printf("插入数据库共耗时: %lu ms\n", msyql_duration);

    /* 清空表 */
    rte_hash_reset(write_upload_ipv4_flow->ipv4_flow_table);
    memset(write_upload_ipv4_flow->ipv4_flow_features, 0, DEFAULT_NUM_IPV4_FLOWS * sizeof(struct IPv4FlowFeature));

    pthread_exit(NULL);
}


/*
 * Write the packets form the write ring into a pcap compressed file
 */
int write_core(const struct core_write_config *config)
{
    void *write_buffer;
    unsigned int packet_length, wire_packet_length, compressed_length;
    unsigned int remaining_bytes;
    int to_write;
    int bytes_to_write;
    struct rte_mbuf *dequeued[DPDKCAP_WRITE_BURST_SIZE];
    struct rte_mbuf *bufptr;
    struct pcap_packet_header header;
    struct timeval tv;
    struct pcap_header pcp;
    int retval = 0;
    int written;
    void *(*file_open_func)(char *);
    int (*file_write_func)(void *, void *, int);
    int (*file_close_func)(void *);

    char file_name[DPDKCAP_OUTPUT_FILENAME_LENGTH];
    unsigned int file_count = 0;
    uint64_t file_size = 0;
    struct timeval file_start;

    if (!config->compression)
    {
        file_open_func = (void *(*)(char *))open_pcap;
        file_write_func = (int (*)(void *, void *, int))write_pcap;
        file_close_func = (int (*)(void *))close_pcap;
    }
    else
    {
        file_open_func = (void *(*)(char *))open_lzo_pcap;
        file_write_func = (int (*)(void *, void *, int))lzowrite;
        file_close_func = (int (*)(void *))close_lzo_pcap;
    }
    gettimeofday(&file_start, NULL);

    // Update filename
    format_from_template(file_name, config->output_file_template, rte_lcore_id(), file_count, &file_start);

    // Init stats
    *(config->stats) = (struct core_write_stats){
        .core_id = rte_lcore_id(),
        .current_file_packets = 0,
        .current_file_bytes = 0,
        .current_file_compressed_bytes = 0,
        .packets_written = 0,
        .packets_filtered = 0,
        .bytes = 0,
        .compressed_bytes = 0,
    };
    memcpy(config->stats->output_file, file_name, DPDKCAP_OUTPUT_FILENAME_LENGTH);

    // Init the common pcap header
    pcap_header_init(&pcp, config->snaplen);

    // Open new file
    write_buffer = file_open_func(file_name);
    if (unlikely(!write_buffer))
    {
        retval = -1;
        goto cleanup;
    }

    // Write pcap header
    written = file_write_func(write_buffer, (unsigned char *)&pcp, sizeof(struct pcap_header));
    if (unlikely(written < 0))
    {
        rte_exit(EXIT_FAILURE, "写文件头失败: %d.\n", written);
        retval = -1;
        goto cleanup;
    }
    file_size = written;

    // Log
    RTE_LOG(INFO, DPDKCAP, "Core %d is writing using file template: %s.\n", rte_lcore_id(), config->output_file_template);

    // 创建IPv4Flow与IPv6Flow的哈希表(只存储Hash(key)作为ipv4_flow_features等的index)
    struct rte_hash *ipv4_flow_hash_tables[TABLES_NUM];
    struct rte_hash *ipv6_flow_hash_tables[TABLES_NUM];
    char flow_hash_name[32] = {0};
    for (int i = 0; i < TABLES_NUM; i++) {
        sprintf(flow_hash_name, "ipv4_flow_hash_%d", i);
        ipv4_flow_hash_tables[i] = create_hash_table((const char *)flow_hash_name, DEFAULT_NUM_IPV4_FLOWS, 12, rte_socket_id());
        if (ipv4_flow_hash_tables[i] == NULL) {
            fprintf(stderr, "Unable to create the hash table: %s\n", flow_hash_name);
            rte_exit(EXIT_FAILURE, "Unable to create the hash table: %s\n", flow_hash_name);
        }
        printf("Hash table %s created, entries: %d, key_len: %d, socket_id: %d\n", flow_hash_name, DEFAULT_NUM_IPV4_FLOWS, 12, rte_socket_id());

        sprintf(flow_hash_name, "ipv6_flow_hash_%d", i);
        ipv6_flow_hash_tables[i] = create_hash_table((const char *)flow_hash_name, DEFAULT_NUM_IPV6_FLOWS, 36, rte_socket_id());
        if (ipv6_flow_hash_tables[i] == NULL) {
            fprintf(stderr, "Unable to create the hash table: %s\n", flow_hash_name);
            rte_exit(EXIT_FAILURE, "Unable to create the hash table: %s\n", flow_hash_name);
        }
        printf("Hash table %s created, entries: %d, key_len: %d, socket_id: %d\n", flow_hash_name, DEFAULT_NUM_IPV6_FLOWS, 36, rte_socket_id());

    }
    // struct rte_hash *ipv4_flow_table = create_hash_table("ipv4_flow_table", DEFAULT_NUM_IPV4_FLOWS, 12, rte_lcore_id());
    // struct rte_hash *ipv6_flow_table = create_hash_table("ipv6_flow_table", DEFAULT_NUM_IPV6_FLOWS, 36, rte_lcore_id());

    // 创建TABLES_NUM个存储IPv4Flow与IPv6Flow特征的数组
    struct IPv4FlowFeature (*ipv4_flow_features)[TABLES_NUM] = (struct IPv4FlowFeature (*)[TABLES_NUM])rte_malloc(NULL, TABLES_NUM * DEFAULT_NUM_IPV4_FLOWS * sizeof(struct IPv4FlowFeature), 0);
    struct IPv6FlowFeature (*ipv6_flow_features)[TABLES_NUM] = (struct IPv6FlowFeature (*)[TABLES_NUM])rte_malloc(NULL, TABLES_NUM * DEFAULT_NUM_IPV6_FLOWS * sizeof(struct IPv6FlowFeature), 0);

    // struct IPv4FlowFeature *ipv4_flow_features = (struct IPv4FlowFeature *)rte_malloc("ipv4 flow features", sizeof(struct IPv4FlowFeature) * DEFAULT_NUM_IPV4_FLOWS, 0);
    // struct IPv6FlowFeature *ipv6_flow_features = (struct IPv6FlowFeature *)rte_malloc("ipv4 flow features", sizeof(struct IPv6FlowFeature) * DEFAULT_NUM_IPV6_FLOWS, 0);

    uint32_t table_id = 0;      // 当前使用的ipv4_flow_features和ipv6_flow_features的index
    struct WriteUploadIpv4Flow write_upload_ipv4_flow[TABLES_NUM];

    uint32_t ipv4_flows_num = 0;
    uint32_t ipv6_flows_num = 0;

    pthread_t write_upload_ipv4_flow_thread[TABLES_NUM];

    for (;;)
    {
        if (unlikely(*(config->stop_condition) && rte_ring_empty(config->ring)))
        { 
            break;
        }

        // Get packets from the ring
#if RTE_VERSION >= RTE_VERSION_NUM(17, 5, 0, 16)
        to_write = rte_ring_dequeue_burst(config->ring, (void *)dequeued,
                                          DPDKCAP_WRITE_BURST_SIZE, NULL);
#else
        to_write = rte_ring_dequeue_burst(config->ring, (void *)dequeued,
                                          DPDKCAP_WRITE_BURST_SIZE);
#endif

        // Update stats
        //  config->stats->packets += to_write;
        int i;
        bool file_changed;

        // printf("Got %d packets from ring\n", to_write);

        for (i = 0; i < to_write; i++)
        {
            // Cast to packet
            bufptr = dequeued[i];
            struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod_offset(bufptr, struct rte_ether_hdr *, 0);

            // 解包
            struct IPv4PktInfo ipv4_pkt;
            struct IPv6PktInfo ipv6_pkt;
            bool is_ipv4 = false;
            bool is_ipv6 = false;
            int ret;
            switch (rte_be_to_cpu_16(eth_hdr->ether_type))
            {
            case RTE_ETHER_TYPE_IPV4:
                is_ipv4 = true;
                ret = parse_ipv4(bufptr, &ipv4_pkt, sizeof(struct rte_ether_hdr));
                if (ret == -1)
                {
                    fprintf(stderr, "Parse ipv4 error\n");
                    RTE_LOG(ERR, DPDKCAP, "Parse ipv4 error\n");
                }
                break;
            case RTE_ETHER_TYPE_IPV6:
                is_ipv6 = true;
                ret = parse_ipv6(bufptr, &ipv6_pkt, sizeof(struct rte_ether_hdr));
                if (ret == -1)
                {
                    fprintf(stderr, "Parse ipv6 error\n");
                    RTE_LOG(ERR, DPDKCAP, "Parse ipv6 error\n");
                }
                break;
            case RTE_ETHER_TYPE_VLAN:;
                struct rte_vlan_hdr *vlan_hdr = rte_pktmbuf_mtod_offset(bufptr, struct rte_vlan_hdr *, sizeof(struct rte_ether_hdr));
                switch (rte_be_to_cpu_16(vlan_hdr->eth_proto))
                {
                case RTE_ETHER_TYPE_IPV4:
                    is_ipv4 = true;
                    ret = parse_ipv4(bufptr, &ipv4_pkt, sizeof(struct rte_ether_hdr) + sizeof(struct rte_vlan_hdr));
                    if (ret == -1)
                    {
                        fprintf(stderr, "Parse ipv4 error\n");
                        RTE_LOG(ERR, DPDKCAP, "Parse ipv4 error\n");
                    }
                    break;
                case RTE_ETHER_TYPE_IPV6:
                    is_ipv6 = true;
                    ret = parse_ipv6(bufptr, &ipv6_pkt, sizeof(struct rte_ether_hdr) + sizeof(struct rte_vlan_hdr));
                    if (ret == -1)
                    {
                        fprintf(stderr, "Parse ipv6 error\n");
                        RTE_LOG(ERR, DPDKCAP, "Parse ipv6 error\n");
                    }
                    break;
                default: // 其他ETHER_TYPE暂不支持
                    break;
                }
                break;
            default: // 其他ETHER_TYPE暂不支持
                break;
            }

            // if (is_ipv4 && !is_ipv6) {
            //     ipv4flow_print(&ipv4_flow);
            // } else if (is_ipv6 && !is_ipv4) {
            //     ipv6flow_print(&ipv6_flow);
            // }

            // 判断是否需要根据流五元组过滤
            if (is_ipv4 && !is_ipv6)
            { // IPv4协议
                // printf("sip: %u, dip: %u, sport: %d, dport: %d, protocol: %d\n", ipv4_pkt.flow.src_ip, ipv4_pkt.flow.dst_ip, ipv4_pkt.flow.src_port, ipv4_pkt.flow.dst_port, ipv4_pkt.flow.proto_id);
                if (config->ip != 0 && config->ip != ipv4_pkt.flow.src_ip && config->ip != ipv4_pkt.flow.dst_ip)
                {
                    rte_pktmbuf_free(dequeued[i]);
                    config->stats->packets_filtered++;
                    continue;
                }
                if (config->ip_upperborder != 0 && (!(ipv4_pkt.flow.src_ip >= config->ip_lowerborder && ipv4_pkt.flow.src_ip <= config->ip_upperborder)) && (!(ipv4_pkt.flow.dst_ip >= config->ip_lowerborder && ipv4_pkt.flow.dst_ip <= config->ip_upperborder)))
                {
                    rte_pktmbuf_free(dequeued[i]);
                    config->stats->packets_filtered++;
                    continue;
                }
                // if (config->protocol != 0 && config->protocol != ipv4_pkt.flow.proto_id)
                // {
                //     rte_pktmbuf_free(dequeued[i]);
                //     config->stats->packets_filtered++;
                //     continue;
                // }
                bool is_proto_in_filter;
                is_proto_in_filter = false;
                for (int i = 0; i < config->num_protocols; i++) 
                {
                    if (ipv4_pkt.flow.proto_id == config->protocols[i]) 
                    {
                        is_proto_in_filter = true;
                        break;
                    }
                }
                if (config->num_protocols > 0 && !is_proto_in_filter)
                {
                    rte_pktmbuf_free(dequeued[i]);
                    config->stats->packets_filtered++;
                    continue;
                }

                bool is_port_in_filter;
                is_port_in_filter = false;
                for (int i = 0; i < config->num_ports; i++) 
                {
                    if (ipv4_pkt.flow.src_port == config->ports[i]) 
                    {
                        is_port_in_filter = true;
                        break;
                    }
                    if (ipv4_pkt.flow.dst_port == config->ports[i]) 
                    {
                        is_port_in_filter = true;
                        break;
                    }
                }
                if (config->num_ports > 0 && !is_port_in_filter)
                {
                    rte_pktmbuf_free(dequeued[i]);
                    config->stats->packets_filtered++;
                    continue;
                }
            }
            else if (is_ipv6 && !is_ipv4)
            { // IPv6协议
                if (config->ip != 0 && memcmp(&config->ip, &ipv6_pkt.flow.src_ip, 16) && memcmp(&config->ip, &ipv6_pkt.flow.dst_ip, 16))
                {
                    rte_pktmbuf_free(dequeued[i]);
                    config->stats->packets_filtered++;
                    continue;
                }
                if (config->protocol != 0 && config->protocol != ipv6_pkt.flow.proto_id)
                {
                    rte_pktmbuf_free(dequeued[i]);
                    config->stats->packets_filtered++;
                    continue;
                }
                if (config->port != 0 && config->port != ipv6_pkt.flow.src_port && config->port != ipv6_pkt.flow.dst_port)
                {
                    rte_pktmbuf_free(dequeued[i]);
                    config->stats->packets_filtered++;
                    continue;
                }
            }

            if (is_ipv4 && !is_ipv6) {
                ret = populate_ipv4_hash_table(ipv4_flow_hash_tables[table_id], &ipv4_pkt, ipv4_flow_features[table_id]);
                if (ret < 0) {
                    fprintf(stderr, "populate ipv4 hash table error\n");
                    RTE_LOG(ERR, DPDKCAP, "populate ipv4 hash table error\n");
                }
            } else if (is_ipv6 && !is_ipv4) {
                populate_ipv6_hash_table(ipv6_flow_hash_tables[table_id], &ipv6_pkt, ipv6_flow_features[table_id]);
                if (ret < 0) {
                    fprintf(stderr, "populate ipv6 hash table error\n");
                    RTE_LOG(ERR, DPDKCAP, "populate ipv6 hash table error\n");
                }
            }

            wire_packet_length = rte_pktmbuf_pkt_len(bufptr);
            // Truncate packet if needed
            packet_length = MIN(config->snaplen, wire_packet_length);

            // Get time
            gettimeofday(&tv, NULL);

            // Create a new file according to limits
            file_changed = 0;
            if (config->rotate_seconds &&
                (uint32_t)(tv.tv_sec - file_start.tv_sec) >= config->rotate_seconds)
            {
                file_count = 0;
                gettimeofday(&file_start, NULL);
                file_changed = 1;
            }
            if (config->file_size_limit && file_size / 1024 / 1024 >= config->file_size_limit)
            {
                file_count++;
                file_changed = 1;
            }

            // Open new file
            if (file_changed)
            {
                // Change file name
                format_from_template(file_name, config->output_file_template, rte_lcore_id(), file_count, &file_start);

                // Update stats
                config->stats->current_file_packets = 0;
                config->stats->current_file_bytes = 0;
                memcpy(config->stats->output_file, file_name,
                       DPDKCAP_OUTPUT_FILENAME_LENGTH);

                // Close pcap file and open new one
                file_close_func(write_buffer);

                // Reopen a file
                write_buffer = file_open_func(file_name);
                if (unlikely(!write_buffer))
                {
                    retval = -1;
                    goto cleanup;
                }

                // Write pcap header
                written = file_write_func(write_buffer, &pcp,
                                          sizeof(struct pcap_header));
                if (unlikely(written < 0))
                {
                    retval = -1;
                    goto cleanup;
                }
                // Reset file size
                file_size = written;
            }

            // Write block header
            header.timestamp = (int32_t)tv.tv_sec;
            header.microseconds = (int32_t)tv.tv_usec;
            header.packet_length = packet_length;
            header.packet_length_wire = wire_packet_length;
            written = file_write_func(write_buffer, &header,
                                      sizeof(struct pcap_packet_header));
            if (unlikely(written < 0))
            {
                retval = -1;
                goto cleanup;
            }
            file_size += written;

            // Write content
            remaining_bytes = packet_length;
            compressed_length = 0;
            while (bufptr != NULL && remaining_bytes > 0)
            {
                bytes_to_write = MIN(rte_pktmbuf_data_len(bufptr), remaining_bytes);
                written = file_write_func(write_buffer,
                                          rte_pktmbuf_mtod(bufptr, void *),
                                          bytes_to_write);
                if (unlikely(written < 0))
                {
                    retval = -1;
                    goto cleanup;
                }
                bufptr = bufptr->next;
                remaining_bytes -= bytes_to_write;
                compressed_length += written;
                file_size += written;
            }

            // Free buffer
            rte_pktmbuf_free(dequeued[i]);

            // Update stats
            config->stats->packets_written++;
            config->stats->bytes += wire_packet_length;
            config->stats->compressed_bytes += compressed_length;
            config->stats->current_file_packets++;
            config->stats->current_file_bytes += packet_length;
            config->stats->current_file_compressed_bytes = file_size;
        }

        /* 如果flow表中的流量超过了阈值，则将其写入文件 */
        uint32_t flow_nums_in_table = rte_hash_count(ipv4_flow_hash_tables[table_id]);
        if (unlikely(flow_nums_in_table > IPV4_FLOWS_LIMIT_IN_TABLE)) {
            /* 更新流数 */
            ipv4_flows_num += flow_nums_in_table;

            write_upload_ipv4_flow[table_id].core_id = rte_lcore_id();
            write_upload_ipv4_flow[table_id].table_id = table_id;
            write_upload_ipv4_flow[table_id].ipv4_flow_table = ipv4_flow_hash_tables[table_id];
            write_upload_ipv4_flow[table_id].ipv4_flow_features = ipv4_flow_features[table_id];
            write_upload_ipv4_flow[table_id].mysql_conn = config->mysql_conn;

            // write_upload_ipv4_flow = {
            //     .core_id = rte_lcore_id(),
            //     .table_id = table_id,
            //     .ipv4_flow_table = ipv4_flow_hash_tables[table_id],
            //     .ipv4_flow_features = ipv4_flow_features[table_id],
            //     .mysql_conn = config->mysql_conn,
            // };

            /* 创建线程执行write_upload_flow */
            // printf("Core id: %d, table id: %d\n", write_upload_ipv4_flow.core_id, write_upload_ipv4_flow.table_id);
            if (pthread_create(&write_upload_ipv4_flow_thread[table_id], NULL, (void *)write_upload_flow, &write_upload_ipv4_flow[table_id]) < 0 ) {
                fprintf(stderr, "create write_upload_flow thread error");
                RTE_LOG(ERR, DPDKCAP, "create write_upload_flow thread error");
                rte_exit(EXIT_FAILURE, "create write_upload_flow thread error");
            }
            // if (rte_eal_remote_launch((lcore_function_t *)write_upload_flow, &write_upload_ipv4_flow, rte_lcore_id()) < 0)
            // {   
            //     printf("Could not launch writing process on lcore %d.\n", core_index);
            //     rte_exit(EXIT_FAILURE, "Could not launch writing process on lcore %d.\n", core_index);
            // }
            
            
            /* 更换表 */
            table_id = (table_id + 1) % TABLES_NUM;

            // /* 将flow表中的特征写入文件 */
            // char csv_file_name[DPDKCAP_OUTPUT_FILENAME_LENGTH] = {0};
            // FILE *fp;
            // sprintf(csv_file_name, "./csv/output_ipv4_%d_%d.csv", rte_lcore_id(), table_id);
            // fp = fopen(csv_file_name, "w");
            // if (unlikely(!fp)) {
            //     RTE_LOG(ERR, DPDKCAP, "Core %d could not open %s in write mode: %d (%s)\n",
            //             rte_lcore_id(), csv_file_name, errno, strerror(errno));
            // }
            // write_ipv4_flow_features_to_csv(ipv4_flow_features[table_id], DEFAULT_NUM_IPV4_FLOWS, fp);
            // fclose(fp);

            // uint64_t mysql_start_time = rte_rdtsc();

            // /* 获取 csv文件的绝对路径 */
            // char csv_file_abpath[DPDKCAP_OUTPUT_FILENAME_LENGTH] = {0};
            // realpath(csv_file_name, csv_file_abpath);
            // if (load_file_into_mysql(config->mysql_conn, MYSQL_TABLE_NAME, csv_file_abpath, DEFAULT_FLOW_SEQ_LEN) != 0) {
            //     return -1;
            // }

            // uint64_t msyql_duration = (rte_rdtsc() - mysql_start_time) * 1000 / rte_get_timer_hz();
            // printf("插入数据库共耗时: %lu ms\n", msyql_duration);

            // /* 清空表 */
            // rte_hash_reset(ipv4_flow_hash_tables[table_id]);
        }

    }

cleanup:
    // Close pcap file
    file_close_func(write_buffer);
    
    RTE_LOG(INFO, DPDKCAP, "Closed writing core %d\n", rte_lcore_id());

    // uint32_t ipv4_size = rte_hash_count(ipv4_flow_hash_tables[table_id]);
    // uint32_t ipv6_size = rte_hash_count(ipv6_flow_hash_tables[table_id]);
    // printf("共有%u条ipv4流, 共有%u条ipv6流\n", ipv4_size, ipv6_size);

    // printf("开始关闭线程\n");
    // pthread_t t1;
    // int pe_res = pthread_equal(t1, 0);
    // printf("线程还未分配: %d\n", pe_res);

    for (int i = 0; i < TABLES_NUM; i++) {
        if (pthread_equal(write_upload_ipv4_flow_thread[i], 0) == 0) {
            continue;
        }
        pthread_join(write_upload_ipv4_flow_thread[i], NULL);   
    }
    // pthread_join(write_upload_ipv4_flow_thread, NULL);

    uint32_t flow_nums_in_table = rte_hash_count(ipv4_flow_hash_tables[table_id]);
    if (likely(flow_nums_in_table > 0)) {
        /* 更新流数 */
        ipv4_flows_num += flow_nums_in_table;
        
        /* 将flow表中的特征写入文件 */
        char csv_file_name[DPDKCAP_OUTPUT_FILENAME_LENGTH];
        FILE *fp;
        sprintf(csv_file_name, "./csv/output_ipv4_%d_%d.csv", rte_lcore_id(), table_id);
        fp = fopen(csv_file_name, "w");
        if (unlikely(!fp)) {
            RTE_LOG(ERR, DPDKCAP, "Core %d could not open %s in write mode: %d (%s)\n",
                    rte_lcore_id(), csv_file_name, errno, strerror(errno));
        }
        write_ipv4_flow_features_to_csv(ipv4_flow_features[table_id], DEFAULT_NUM_IPV4_FLOWS, fp);
        fclose(fp);

        uint64_t mysql_start_time = rte_rdtsc();
        char csv_file_abpath[DPDKCAP_OUTPUT_FILENAME_LENGTH] = {0};
        realpath(csv_file_name, csv_file_abpath);
        if (load_file_into_mysql(config->mysql_conn, MYSQL_TABLE_NAME, csv_file_abpath, DEFAULT_FLOW_SEQ_LEN) != 0) {
            return -1;
        }

        uint64_t msyql_duration = (rte_rdtsc() - mysql_start_time) * 1000 / rte_get_timer_hz();
        printf("插入数据库共耗时: %lu ms\n", msyql_duration);
    }

    flow_nums_in_table = rte_hash_count(ipv6_flow_hash_tables[table_id]);
    if (likely(flow_nums_in_table > 0)) {
        /* 更新流数 */
        ipv6_flows_num += flow_nums_in_table;
        
        /* 将flow表中的特征写入文件 */
        char csv_file_name[DPDKCAP_OUTPUT_FILENAME_LENGTH];
        FILE *fp;
        sprintf(csv_file_name, "./csv/output_ipv6_%d_%d.csv", rte_lcore_id(), table_id);
        fp = fopen(csv_file_name, "w");
        if (unlikely(!fp)) {
            RTE_LOG(ERR, DPDKCAP, "Core %d could not open %s in write mode: %d (%s)\n",
                    rte_lcore_id(), csv_file_name, errno, strerror(errno));
        }
        write_ipv6_flow_features_to_csv(ipv6_flow_features[table_id], DEFAULT_NUM_IPV6_FLOWS, fp);
        fclose(fp);
    }

    printf("Core %d finished, ipv4 flows num: %u, ipv6 flows num: %u\n", rte_lcore_id(), ipv4_flows_num, ipv6_flows_num);
    

    // // 将流特征写入csv文件
    // char csv_file_name[DPDKCAP_OUTPUT_FILENAME_LENGTH];
    // FILE *fp;

    // // 写入IPv4流特征
    // if (ipv4_size > 0) {
    //     sprintf(csv_file_name, "./csv/output_ipv4_%d_%d.csv", rte_lcore_id(), table_id);
    //     fp = fopen(csv_file_name, "w");
    //     if (unlikely(!fp)) {
    //         RTE_LOG(ERR, DPDKCAP, "Core %d could not open %s in write mode: %d (%s)\n",
    //                 rte_lcore_id(), csv_file_name, errno, strerror(errno));
    //     }
    //     write_ipv4_flow_features_to_csv(ipv4_flow_features[table_id], DEFAULT_NUM_IPV4_FLOWS, fp);
    //     fclose(fp);
    // }

    // // 写入IPv6流特征
    // if (ipv6_size > 0) {
    //     sprintf(csv_file_name, "./csv/output_ipv6_%d_%d.csv", rte_lcore_id(), table_id);
    //     fp = fopen(csv_file_name, "w");
    //     if (unlikely(!fp)) {
    //         RTE_LOG(ERR, DPDKCAP, "Core %d could not open %s in write mode: %d (%s)\n",
    //                 rte_lcore_id(), csv_file_name, errno, strerror(errno));
    //     }
    //     write_ipv6_flow_features_to_csv(ipv6_TABLES_NUM[table_id], DEFAULT_NUM_IPV6_FLOWS, fp);
    //     fclose(fp);
    // }

    // // 提交到MySQL数据库
    // MYSQL mysql_conn;

    // if (collect_mysql(&mysql_conn) != 0) {
    //     return -1;
    // }

    // if (create_table(&mysql_conn, MYSQL_TABLE_NAME) != 0) {
    //     return -1;
    // }

    // if (show_tables(&mysql_conn) != 0) {
    //     return -1;
    // }


    // if (insert_data_direct(&mysql_conn, "Device_3544302408", ipv4_flow_features, DEFAULT_NUM_IPV4_FLOWS) != 0) {
    //     return -1;
    // }

    // uint64_t mysql_start_time = rte_rdtsc();


    // if (load_file_into_mysql(&mysql_conn, MYSQL_TABLE_NAME, "/home/【User】/jkzhang/dpdk-anomaly-flow-detector/csv/output_ipv4_1.csv", DEFAULT_FLOW_SEQ_LEN) != 0) {
    //     return -1;
    // }

    // uint64_t msyql_duration = (rte_rdtsc() - mysql_start_time) * 1000 / rte_get_timer_hz();
    // printf("插入数据库共耗时: %lu ms\n", msyql_duration);

    // mysql_close(&mysql_conn);    // 关闭连接
    // mysql_library_end();         // 关闭MySQL库

    // 释放空间
    for (int i = 0; i < TABLES_NUM; i++ ) {
        rte_free(ipv4_flow_features[i]);
        rte_free(ipv6_flow_features[i]);

        rte_hash_free(ipv4_flow_hash_tables[i]);
        rte_hash_free(ipv6_flow_hash_tables[i]);
    }

    return retval;
}
