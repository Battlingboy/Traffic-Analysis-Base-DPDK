#include <rte_ring.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_mbuf.h>
#include <rte_branch_prediction.h>
#include <rte_version.h>
#include <rte_common.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_tcp.h>
#include <rte_jhash.h>
#include <rte_hash_crc.h>

#include "flow_extractor.h"

#define DEFAULT_HASH_FUNC       rte_jhash

int parse_ipv4(struct rte_mbuf * mbuf, struct IPv4PktInfo * pkt, uint16_t offset) {
    pkt->pkt_timestamp = rte_rdtsc();

    struct rte_ipv4_hdr *ipv4_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_ipv4_hdr *, offset);
    pkt->flow.src_ip = rte_be_to_cpu_32(ipv4_hdr->src_addr);
    pkt->flow.dst_ip = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
    pkt->flow.proto_id = ipv4_hdr->next_proto_id;
    switch (ipv4_hdr->next_proto_id) {
        case IPPROTO_TCP:;
            struct rte_tcp_hdr *tcp_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_tcp_hdr *, offset + sizeof(struct rte_ipv4_hdr));
            pkt->flow.src_port = rte_be_to_cpu_16(tcp_hdr->src_port);
            pkt->flow.dst_port = rte_be_to_cpu_16(tcp_hdr->dst_port);
            pkt->pdu_len = rte_be_to_cpu_16(ipv4_hdr->total_length) - (ipv4_hdr->ihl) * 4 - (tcp_hdr->data_off >> 4) * 4;
            break;
        case IPPROTO_UDP:;
            struct rte_udp_hdr *udp_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_udp_hdr *, offset + sizeof(struct rte_ipv4_hdr));
            pkt->flow.src_port = rte_be_to_cpu_16(udp_hdr->src_port);
            pkt->flow.dst_port = rte_be_to_cpu_16(udp_hdr->dst_port);
            pkt->pdu_len = rte_be_to_cpu_16(udp_hdr->dgram_len) - 8;
            break;
        default:        // 其他协议暂不支持
            pkt->flow.src_port = 0;
            pkt->flow.dst_port = 0;
            pkt->pdu_len = 0;
            break;
    }

    return 0;
}

int parse_ipv6(struct rte_mbuf * mbuf, struct IPv6PktInfo * pkt, uint16_t offset) {
    pkt->pkt_timestamp = rte_rdtsc();

    struct rte_ipv6_hdr *ipv6_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_ipv6_hdr *, offset);
    rte_memcpy(pkt->flow.src_ip, ipv6_hdr->src_addr, sizeof(ipv6_hdr->src_addr));
    rte_memcpy(pkt->flow.dst_ip, ipv6_hdr->dst_addr, sizeof(ipv6_hdr->dst_addr));
    pkt->flow.proto_id = ipv6_hdr->proto;
    switch (ipv6_hdr->proto) {
        case IPPROTO_TCP:;
            struct rte_tcp_hdr *tcp_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_tcp_hdr *, offset + sizeof(struct rte_ipv6_hdr));
            pkt->flow.src_port = rte_be_to_cpu_16(tcp_hdr->src_port);
            pkt->flow.dst_port = rte_be_to_cpu_16(tcp_hdr->dst_port);
            pkt->pdu_len = rte_be_to_cpu_16(ipv6_hdr->payload_len) - (tcp_hdr->data_off >> 4) * 4;
            break;
        case IPPROTO_UDP:;
            struct rte_udp_hdr *udp_hdr = rte_pktmbuf_mtod_offset(mbuf, struct rte_udp_hdr *, offset + sizeof(struct rte_ipv6_hdr));
            pkt->flow.src_port = rte_be_to_cpu_16(udp_hdr->src_port);
            pkt->flow.dst_port = rte_be_to_cpu_16(udp_hdr->dst_port);
            pkt->pdu_len = rte_be_to_cpu_16(udp_hdr->dgram_len) - 8;
            break;
        default:        // 其他协议暂不支持
            pkt->flow.src_port = 0;
            pkt->flow.dst_port = 0;
            pkt->pdu_len = 0;
            break;
    }

    return 0;
}

bool ipv4_flow_equal(const struct IPv4FlowTuple * flow1, const struct IPv4FlowTuple * flow2) {
    if (flow1->src_ip != flow2->src_ip && flow1->src_ip != flow2->dst_ip) {
        return false;
    }
    if (flow1->dst_ip != flow2->src_ip && flow1->dst_ip != flow2->dst_ip) {
        return false;
    }
    if (flow1->src_port != flow2->src_port && flow1->src_port != flow2->dst_port) {
        return false;
    }
    if (flow1->dst_port != flow2->src_port && flow1->dst_port != flow2->dst_port) {
        return false;
    }
    if (flow1->proto_id != flow2->proto_id) {
        return false;
    }

    return true;
}

void ipv4flow_print(struct IPv4FlowTuple * flow) {
    char a, b, c, d;

    // %hhu输出占一个字节，%hu输出占两个字节
    uint32_t_to_char(rte_bswap32(flow->src_ip), &a, &b, &c, &d);
    printf("src: %3hhu.%3hhu.%3hhu.%3hhu \t", a, b, c, d);

    uint32_t_to_char(rte_bswap32(flow->dst_ip), &a, &b, &c, &d);
    printf("dst: %3hhu.%3hhu.%3hhu.%3hhu \t", a, b, c, d);

    printf("src port: %5hu \tdst port: %5hu \tprotocol: %3hhu\n", flow->src_port, flow->dst_port, flow->proto_id);
}

void ipv6flow_print(struct IPv6FlowTuple * flow) {
    uint8_t *addr;

    addr = flow->src_ip;
    printf("src: %4hx:%4hx:%4hx:%4hx:%4hx:%4hx:%4hx:%4hx\t",
           (uint16_t)((addr[0] << 8) | addr[1]),
           (uint16_t)((addr[2] << 8) | addr[3]),
           (uint16_t)((addr[4] << 8) | addr[5]),
           (uint16_t)((addr[6] << 8) | addr[7]),
           (uint16_t)((addr[8] << 8) | addr[9]),
           (uint16_t)((addr[10] << 8) | addr[11]),
           (uint16_t)((addr[12] << 8) | addr[13]),
           (uint16_t)((addr[14] << 8) | addr[15]));

    addr = flow->dst_ip;
    printf("dst: %4hx:%4hx:%4hx:%4hx:%4hx:%4hx:%4hx:%4hx",
           (uint16_t)((addr[0] << 8) | addr[1]),
           (uint16_t)((addr[2] << 8) | addr[3]),
           (uint16_t)((addr[4] << 8) | addr[5]),
           (uint16_t)((addr[6] << 8) | addr[7]),
           (uint16_t)((addr[8] << 8) | addr[9]),
           (uint16_t)((addr[10] << 8) | addr[11]),
           (uint16_t)((addr[12] << 8) | addr[13]),
           (uint16_t)((addr[14] << 8) | addr[15]));

    printf("src port: %5hu \tdst port: %5hu \tprotocol: %3hhu\n", flow->src_port, flow->dst_port, flow->proto_id);
}

// 格式化IPv4流
void ipv4flow_format(struct IPv4FlowTuple * flow, char * str) {
    char a, b, c, d;

    uint32_t_to_char(rte_bswap32(flow->src_ip), &a, &b, &c, &d);
    sprintf(str, "%3hhu.%3hhu.%3hhu.%3hhu,", a, b, c, d);

    uint32_t_to_char(rte_bswap32(flow->dst_ip), &a, &b, &c, &d);
    sprintf(str + strlen(str), "%3hhu.%3hhu.%3hhu.%3hhu,%5hu,%5hu,%3hhu", a, b, c, d, flow->src_port, flow->dst_port, flow->proto_id);
}

// 格式化IPv6流
void ipv6flow_format(struct IPv6FlowTuple * flow, char * str) {
    uint8_t *addr;

    addr = flow->src_ip;
    sprintf(str, "%4hx:%4hx:%4hx:%4hx:%4hx:%4hx:%4hx:%4hx,",
            (uint16_t)((addr[0] << 8) | addr[1]),
            (uint16_t)((addr[2] << 8) | addr[3]),
            (uint16_t)((addr[4] << 8) | addr[5]),
            (uint16_t)((addr[6] << 8) | addr[7]),
            (uint16_t)((addr[8] << 8) | addr[9]),
            (uint16_t)((addr[10] << 8) | addr[11]),
            (uint16_t)((addr[12] << 8) | addr[13]),
            (uint16_t)((addr[14] << 8) | addr[15]));

    addr = flow->dst_ip;
    sprintf(str + strlen(str), "%4hx:%4hx:%4hx:%4hx:%4hx:%4hx:%4hx:%4hx,%5hu,%5hu,%3hhu",
            (uint16_t)((addr[0] << 8) | addr[1]),
            (uint16_t)((addr[2] << 8) | addr[3]),
            (uint16_t)((addr[4] << 8) | addr[5]),
            (uint16_t)((addr[6] << 8) | addr[7]),
            (uint16_t)((addr[8] << 8) | addr[9]),
            (uint16_t)((addr[10] << 8) | addr[11]),
            (uint16_t)((addr[12] << 8) | addr[13]),
            (uint16_t)((addr[14] << 8) | addr[15]),
            flow->src_port, flow->dst_port, flow->proto_id);
}

// 格式化IPv4FlowTuple字符串并返回字符串
char * ipv4flow_format_str(struct IPv4FlowTuple * flow) {
    char * str = (char *)malloc(100);
    ipv4flow_format(flow, str);
    return str;
}

// 格式化IPv6FlowTuple字符串并返回字符串
char * ipv6flow_format_str(struct IPv6FlowTuple * flow) {
    char * str = (char *)malloc(100);
    ipv6flow_format(flow, str);
    return str;
}

// 创建hash表
struct rte_hash * create_hash_table(const char * name, uint32_t entries, uint32_t key_len, uint32_t socket_id) {
    struct rte_hash_parameters hash_params = {
        .name = name,
        .entries = entries,
        .key_len = key_len,
        .socket_id = socket_id,
        .hash_func = rte_hash_crc,
        .hash_func_init_val = 0,
    };

    struct rte_hash * hash_table = rte_hash_create(&hash_params);

    return hash_table;
}

// 更新IPv4 hash表
int populate_ipv4_hash_table(struct rte_hash *hash_table, struct IPv4PktInfo *pkt, struct IPv4FlowFeature *ipv4_flow_features) {
    int ret = rte_hash_add_key(hash_table, &(pkt->flow));
    if (ret < 0) {
        if (ret == -ENOSPC) {
            fprintf(stderr, "Hash table is full\n");
        }
        fprintf(stderr, "Unable to add key to the hash table\n");
        return ret;
    } else {
        // 判断是否是流的第一个包
        uint16_t pkt_count = ipv4_flow_features[ret].pkt_count;
        if (pkt_count == 0) {
            ipv4_flow_features[ret].flow = pkt->flow;
            ipv4_flow_features[ret].start_time = time(NULL);
            ipv4_flow_features[ret].first_pkt_time = pkt->pkt_timestamp;
            ipv4_flow_features[ret].last_pkt_time = pkt->pkt_timestamp;
            ipv4_flow_features[ret].pkt_count = 1;
            ipv4_flow_features[ret].bytes_count = pkt->pdu_len;
            ipv4_flow_features[ret].pdu_lens[0] = pkt->pdu_len;
        } else {
            ipv4_flow_features[ret].last_pkt_time = pkt->pkt_timestamp;
            if (pkt_count < DEFAULT_FLOW_SEQ_LEN) {
                ipv4_flow_features[ret].pdu_lens[pkt_count] = pkt->pdu_len;
            }
            ipv4_flow_features[ret].pkt_count++;
            ipv4_flow_features[ret].bytes_count += pkt->pdu_len;
        }
    }

    return 0;
}

// 更新IPv6 hash表
int populate_ipv6_hash_table(struct rte_hash *hash_table, struct IPv6PktInfo *pkt, struct IPv6FlowFeature *ipv6_flow_features) {
    int ret = rte_hash_add_key(hash_table, &(pkt->flow));
    if (ret < 0) {
        if (ret == -ENOSPC) {
            fprintf(stderr, "Hash table is full\n");
        }
        fprintf(stderr, "Unable to add key to the hash table\n");
        return ret;
    } else {
        // 判断是否是流的第一个包
        uint16_t pkt_count = ipv6_flow_features[ret].pkt_count;
        if (pkt_count == 0) {
            ipv6_flow_features[ret].flow = pkt->flow;
            ipv6_flow_features[ret].start_time = time(NULL);
            ipv6_flow_features[ret].first_pkt_time = pkt->pkt_timestamp;
            ipv6_flow_features[ret].last_pkt_time = pkt->pkt_timestamp;
            ipv6_flow_features[ret].pdu_lens[0] = pkt->pdu_len;
            ipv6_flow_features[ret].pkt_count = 1;
            ipv6_flow_features[ret].bytes_count = pkt->pdu_len;
        } else {
            ipv6_flow_features[ret].last_pkt_time = pkt->pkt_timestamp;
            if (pkt_count < DEFAULT_FLOW_SEQ_LEN) {
                ipv6_flow_features[ret].pdu_lens[pkt_count] = pkt->pdu_len;
            }
            ipv6_flow_features[ret].pkt_count++;
            ipv6_flow_features[ret].bytes_count += pkt->pdu_len;
        }
    }
    
    return 0;
}

// 将IPv4流特征写入CSV文件
int write_ipv4_flow_features_to_csv(struct IPv4FlowFeature *ipv4_flow_features, uint32_t num_ipv4_flows, FILE *fp) {
    // 写入表头
    // fprintf(fp, "Flow Tuple,Duration,Packet Count");
    fprintf(fp, "time,sip,dip,sport,dport,protocol,pkts,bytes,duration");
    for(int i = 1; i <= DEFAULT_FLOW_SEQ_LEN; i++) {
        fprintf(fp, ",PDU%d", i);
    }
    // fprintf(fp, "\n");

    // char flow_str[DEFAULT_FLOW_STR_LEN];
    uint64_t flow_duration;
    uint8_t pkt_count;

    for (uint32_t i = 0; i < num_ipv4_flows; i++) {
        pkt_count = ipv4_flow_features[i].pkt_count;
        if (pkt_count == 0) {
            continue;
        }

        flow_duration = (ipv4_flow_features[i].last_pkt_time - ipv4_flow_features[i].first_pkt_time) * 1000 / rte_get_timer_hz();

        fprintf(fp, "\n%lu,%u,%u,%hu,%hu,%hu,%u,%u,%u", ipv4_flow_features[i].start_time, ipv4_flow_features[i].flow.src_ip, ipv4_flow_features[i].flow.dst_ip,
            ipv4_flow_features[i].flow.src_port, ipv4_flow_features[i].flow.dst_port, ipv4_flow_features[i].flow.proto_id, ipv4_flow_features[i].pkt_count,
            ipv4_flow_features[i].bytes_count, (uint32_t)flow_duration);

        // // 将IPv4流转换为字符串
        // ipv4flow_format(&ipv4_flow_features[i].flow, flow_str);

        // // 计算流持续时间
        // flow_duration = (ipv4_flow_features[i].last_pkt_time - ipv4_flow_features[i].first_pkt_time) * 1000 / rte_get_timer_hz();

        // // 将流特征写入CSV文件
        // fprintf(fp, "\n%s,%lu,%hu", flow_str, flow_duration, pkt_count);

        // pkt_count = (pkt_count < DEFAULT_FLOW_SEQ_LEN) ? pkt_count : DEFAULT_FLOW_SEQ_LEN;
        for (int j = 0; j < DEFAULT_FLOW_SEQ_LEN; j++) {
            fprintf(fp, ",%hu", ipv4_flow_features[i].pdu_lens[j]);
        }

        // 清空该流特征
        ipv4_flow_features[i].pkt_count = 0;
    }

    return 0;
}     

// 将IPv6流特征写入CSV文件
int write_ipv6_flow_features_to_csv(struct IPv6FlowFeature *ipv6_flow_features, uint32_t num_ipv6_flows, FILE *fp) {
    // 写入表头
    // fprintf(fp, "Flow Tuple,Duration,Packet Count");
    fprintf(fp, "Src Adress,Dst Adress,Src Port,Dst Port,Protocol,Duration,Packet Count");
    for(int i = 1; i <= DEFAULT_FLOW_SEQ_LEN; i++) {
        fprintf(fp, ",PDU Len %d", i);
    }

    char flow_str[DEFAULT_FLOW_STR_LEN];
    uint64_t flow_duration;
    uint8_t pkt_count;

    for (uint32_t i = 0; i < num_ipv6_flows; i++) {
        pkt_count = ipv6_flow_features[i].pkt_count;
        if (pkt_count == 0) {
            continue;
        }

        // 将IPv4流转换为字符串
        ipv6flow_format(&ipv6_flow_features[i].flow, flow_str);

        // 计算流持续时间
        flow_duration = (ipv6_flow_features[i].last_pkt_time - ipv6_flow_features[i].first_pkt_time) * 1000 / rte_get_timer_hz();

        // 将流特征写入CSV文件
        fprintf(fp, "\n%s,%lu,%hu", flow_str, flow_duration, pkt_count);
        // pkt_count = (pkt_count < DEFAULT_FLOW_SEQ_LEN) ? pkt_count : DEFAULT_FLOW_SEQ_LEN;
        for (int j = 0; j < DEFAULT_FLOW_SEQ_LEN; j++) {
            fprintf(fp, ",%hu", ipv6_flow_features[i].pdu_lens[j]);
        }
    }

    return 0;
}   


// int add_ipv4_hash_table(const struct rte_hash *hash_table, struct IPv4FlowTuple *flow) {
//     int ret = 0;

//     ret = rte_hash_lookup(hash_table, flow);
//     if (ret < 0) {
//         if (ret == -ENOENT) {
//             ret = rte_hash_add_key_data(hash_table, flow, 1);
//             if (ret < 0) {
//                 rte_exit(EXIT_FAILURE, "Unable to add the entry to the hash table\n");
//                 return ret;
//             } else {
//                 return 0;
//             }
//         } else {
//             rte_exit(EXIT_FAILURE, "Unable to lookup this flow in the table\n");
//             return ret;
//         }
//     } else {
//         printf("ret: %d, %d\n", ret);
//         return 0;
//     }
// }

// int add_ipv6_hash_table(const struct rte_hash *hash_table, struct IPv6FlowTuple *flow) {
//     int ret = 0;

//     ret = rte_hash_lookup(hash_table, flow);
//     if (ret < 0) {
//         if (ret == -ENOENT) {
//             ret = rte_hash_add_key_data(hash_table, flow, 1);
//             if (ret < 0) {
//                 rte_exit(EXIT_FAILURE, "Unable to add the entry to the hash table\n");
//                 return ret;
//             } else {
//                 return 0;
//             }
//         } else {
//             rte_exit(EXIT_FAILURE, "Unable to lookup this flow in the table\n");
//             return ret;
//         }
//     } else {
//         printf("ret: %d\n", ret);
//         return 0;
//     }
// }