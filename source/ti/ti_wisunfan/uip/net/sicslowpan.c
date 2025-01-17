/**
 * \addtogroup sicslowpan
 * @{
 */
/*
 * Copyright (c) 2008, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */
/**
 * \file
 *         6lowpan implementation (RFC4944 and draft-ietf-6lowpan-hc-06)
 *
 * \author Adam Dunkels <adam@sics.se>
 * \author Nicolas Tsiftes <nvt@sics.se>
 * \author Niclas Finne <nfi@sics.se>
 * \author Mathilde Durvy <mdurvy@cisco.com>
 * \author Julien Abeille <jabeille@cisco.com>
 * \author Joakim Eriksson <joakime@sics.se>
 * \author Joel Hoglund <joel@sics.se>
 */
/******************************************************************************
 *
 * Copyright (c) 2014 Texas Instruments Inc.  All rights reserved.
 *
 * DESCRIPTION:
 *
 * HISTORY:
 *
 *
 ******************************************************************************/

/**
 * FOR HC-06 COMPLIANCE TODO:
 * -Add compression options to UDP, currently only supports
 *  both ports compressed or both ports elided
 *
 * -Verify TC/FL compression works
 *
 * -Add stateless multicast option
 */

#include "uip-conf.h"
#include "uip_rpl_process.h"
#if WITH_SICSLOWPAN

#include <string.h>

#include "net/tcpip.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/packetbuf.h"
#include "net/queuebuf.h"
#include "net/sicslowpan.h"
#if 0
#include "rime/rimeaddr.h"
#include "sys/timer.h"
#endif
#ifdef DEBUG  
#undef DEBUG
#endif
#define DEBUG DEBUG_NONE
#include "net/uip-debug.h"

#if ORIGINAL_CONTIKI
#include "net/netstack.h"
#endif

#ifndef LINUX_GATEWAY
//#include "ulpsmac.h"
#endif

#ifdef SICSLOWPAN_CONF_MAX_MAC_TRANSMISSIONS
#define SICSLOWPAN_MAX_MAC_TRANSMISSIONS SICSLOWPAN_CONF_MAX_MAC_TRANSMISSIONS
#else
#define SICSLOWPAN_MAX_MAC_TRANSMISSIONS 4
#endif

#ifndef SICSLOWPAN_COMPRESSION
#ifdef SICSLOWPAN_CONF_COMPRESSION
#define SICSLOWPAN_COMPRESSION SICSLOWPAN_CONF_COMPRESSION
#else
#define SICSLOWPAN_COMPRESSION SICSLOWPAN_COMPRESSION_IPV6
#endif /* SICSLOWPAN_CONF_COMPRESSION */
#endif /* SICSLOWPAN_COMPRESSION */

#define GET16(ptr,index) (((uint16_t)((ptr)[index] << 8)) | ((ptr)[(index) + 1]))
#define SET16(ptr,index,value) do {     \
(ptr)[index] = ((value) >> 8) & 0xff; \
    (ptr)[index + 1] = (value) & 0xff;    \
} while(0)

/** \name Pointers in the rime buffer
 *  @{
 */
#define RIME_FRAG_PTR           (rime_ptr)
#define RIME_FRAG_DISPATCH_SIZE 0   /* 16 bit */
#define RIME_FRAG_TAG           2   /* 16 bit */
#define RIME_FRAG_OFFSET        4   /* 8 bit */

/* define the buffer as a byte array */
#define RIME_IPHC_BUF              ((uint8_t *)(rime_ptr + rime_hdr_len))

#define RIME_HC1_PTR            (rime_ptr + rime_hdr_len)
#define RIME_HC1_DISPATCH       0 /* 8 bit */
#define RIME_HC1_ENCODING       1 /* 8 bit */
#define RIME_HC1_TTL            2 /* 8 bit */

#define RIME_HC1_HC_UDP_PTR           (rime_ptr + rime_hdr_len)
#define RIME_HC1_HC_UDP_DISPATCH      0 /* 8 bit */
#define RIME_HC1_HC_UDP_HC1_ENCODING  1 /* 8 bit */
#define RIME_HC1_HC_UDP_UDP_ENCODING  2 /* 8 bit */
#define RIME_HC1_HC_UDP_TTL           3 /* 8 bit */
#define RIME_HC1_HC_UDP_PORTS         4 /* 8 bit */
#define RIME_HC1_HC_UDP_CHKSUM        5 /* 16 bit */

/** \name Pointers in the sicslowpan and uip buffer
 *  @{
 */
#define SICSLOWPAN_IP_BUF   ((struct uip_ip_hdr *)&sicslowpan_buf(reass_index)[UIP_LLH_LEN])
#define SICSLOWPAN_UDP_BUF ((struct uip_udp_hdr *)&sicslowpan_buf(reass_index)[UIP_LLIPH_LEN])

#define UIP_IP_BUF          ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF          ((struct uip_udp_hdr *)&uip_buf[UIP_LLIPH_LEN])
#define UIP_TCP_BUF          ((struct uip_tcp_hdr *)&uip_buf[UIP_LLIPH_LEN])
#define UIP_ICMP_BUF          ((struct uip_icmp_hdr *)&uip_buf[UIP_LLIPH_LEN])
#define UIP_IP_INPUT_BUF          ((struct uip_ip_hdr *)&uip_input_buf[UIP_LLH_LEN])
/** @} */

/** \brief Size of the 802.15.4 payload (127byte - 25 for MAC header) */
#ifdef MAX_PACKET_TEST
#define SICSLOWPAN_CONF_MAC_MAX_PAYLOAD 2000
#endif

#ifdef SICSLOWPAN_CONF_MAC_MAX_PAYLOAD
#define MAC_MAX_PAYLOAD SICSLOWPAN_CONF_MAC_MAX_PAYLOAD
#else /* SICSLOWPAN_CONF_MAC_MAX_PAYLOAD */
#define MAC_MAX_PAYLOAD         102       //fragmentation size
#endif /* SICSLOWPAN_CONF_MAC_MAX_PAYLOAD */

/** \brief Some MAC layers need a minimum payload, which is
 configurable through the SICSLOWPAN_CONF_MIN_MAC_PAYLOAD
 option. */
#ifdef SICSLOWPAN_CONF_COMPRESSION_THRESHOLD
#define COMPRESSION_THRESHOLD SICSLOWPAN_CONF_COMPRESSION_THRESHOLD
#else
#define COMPRESSION_THRESHOLD 0
#endif

/** \name General variables
 *  @{
 */
#ifdef SICSLOWPAN_NH_COMPRESSOR
/** A pointer to the additional compressor */
extern struct sicslowpan_nh_compressor SICSLOWPAN_NH_COMPRESSOR;
#endif

/**
 * A pointer to the rime buffer.
 * We initialize it to the beginning of the rime buffer, then
 * access different fields by updating the offset rime_hdr_len.
 */
uint8_t *rime_ptr;

/**
 * rime_hdr_len is the total length of (the processed) 6lowpan headers
 * (fragment headers, IPV6 or HC1, HC2, and HC1 and HC2 non compressed
 * fields).
 */
uint8_t rime_hdr_len;

/**
 * The length of the payload in the Rime buffer.
 * The payload is what comes after the compressed or uncompressed
 * headers (can be the IP payload if the IP header only is compressed
 * or the UDP payload if the UDP header is also compressed)
 */
uint16_t rime_payload_len;

/**
 * uncomp_hdr_len is the length of the headers before compression (if HC2
 * is used this includes Nthe UDP header in addition to the IP header).
 */
static uint8_t uncomp_hdr_len;

/**
 * the result of the last transmitted fragment
 */
//static int last_tx_status;
/** @} */

#if SICSLOWPAN_CONF_FRAG
/** \name Fragmentation related variables
 *  @{
 */

//static uint16_t sicslowpan_len;
/**
 * The buffer array used for the 6lowpan reassembly.
 * The buffers contain only the IPv6 packet (no MAC header, 6lowpan, etc).
 * It has a fix size as we do not use dynamic memory allocation.
 */

//REASS_BUF_SIZE must be at least 2
//will support REASS_BUF_SIZE - 1 # of fragmented packets
#ifdef SICSLOWPAN_CONF_REASS_BUF_SIZE
#define REASS_BUF_SIZE SICSLOWPAN_CONF_REASS_BUF_SIZE
#else
#define REASS_BUF_SIZE 3
#endif

//reass reassembly buffers
reass_struct_t reass_structs[REASS_BUF_SIZE];
#define sicslowpan_buf(x) (reass_structs[x].aligned_buf.u8)

//count for # of reass buffers in use
uint8_t reass_count;
//index into reassembly buffer array
uint8_t reass_index = 0;

/** The total length of the IPv6 packet in the sicslowpan_buf. */

/**
 * length of the ip packet already sent / received.
 * It includes IP and transport headers.
 */
//static uint16_t processed_ip_in_len;
/** Datagram tag to be put in the fragments I send. */
static uint16_t my_tag;

/** When reassembling, the tag in the fragments being merged. */
//static uint16_t reass_tag;
/** When reassembling, the source address of the fragments being merged */
//rimeaddr_t frag_sender;
/** Reassembly %process %timer. */
//static struct timer reass_timer;
/** @} */
#else /* SICSLOWPAN_CONF_FRAG */
/** The buffer used for the 6lowpan processing is uip_buf.
 We do not use any additional buffer.*/
#define sicslowpan_buf uip_buf
#define sicslowpan_len uip_len
#endif /* SICSLOWPAN_CONF_FRAG */

#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06
/** \name HC06 specific variables
 *  @{
 */

/** Addresses contexts for IPHC. */
#if SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS > 0
static struct sicslowpan_addr_context addr_contexts[SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS];
#endif

/** pointer to an address context. */
static struct sicslowpan_addr_context *context;

/** pointer to the byte where to write next inline field. */
static uint8_t *hc06_ptr;

/* Uncompression of linklocal */
/*   0 -> 16 bytes from packet  */
/*   1 -> 2 bytes from prefix - bunch of zeroes and 8 from packet */
/*   2 -> 2 bytes from prefix - 0000::00ff:fe00:XXXX from packet */
/*   3 -> 2 bytes from prefix - infer 8 bytes from lladdr */
/*   NOTE: => the uncompress function does change 0xf to 0x10 */
/*   NOTE: 0x00 => no-autoconfig => unspecified */
const uint8_t unc_llconf[] = { 0x0f, 0x28, 0x22, 0x20 };

/* Uncompression of ctx-based */
/*   0 -> 0 bits from packet [unspecified / reserved] */
/*   1 -> 8 bytes from prefix - bunch of zeroes and 8 from packet */
/*   2 -> 8 bytes from prefix - 0000::00ff:fe00:XXXX + 2 from packet */
/*   3 -> 8 bytes from prefix - infer 8 bytes from lladdr */
const uint8_t unc_ctxconf[] = { 0x00, 0x88, 0x82, 0x80 };

/* Uncompression of ctx-based */
/*   0 -> 0 bits from packet  */
/*   1 -> 2 bytes from prefix - bunch of zeroes 5 from packet */
/*   2 -> 2 bytes from prefix - zeroes + 3 from packet */
/*   3 -> 2 bytes from prefix - infer 1 bytes from lladdr */
const uint8_t unc_mxconf[] = { 0x0f, 0x25, 0x23, 0x21 };

/* Link local prefix */
const uint8_t llprefix[] = { 0xfe, 0x80 };

/* TTL uncompression values */
static const uint8_t ttl_values[] = { 0, 1, 64, 255 };

/*--------------------------------------------------------------------*/
/** \name HC06 related functions
 * @{                                                                 */
/*--------------------------------------------------------------------*/
/** \brief find the context corresponding to prefix ipaddr */
static struct sicslowpan_addr_context*
addr_context_lookup_by_prefix(uip_ipaddr_t *ipaddr)
{
    /* Remove code to avoid warnings and save flash if no context is used */
#if SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS > 0
    int i;
    for (i = 0; i < SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS; i++)
    {
        if ((addr_contexts[i].used == 1)
                && uip_ipaddr_prefixcmp(&addr_contexts[i].prefix, ipaddr, 64))
        {
            return &addr_contexts[i];
        }
    }
#endif /* SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS > 0 */
    return NULL;
}
/*--------------------------------------------------------------------*/
/** \brief find the context with the given number */
static struct sicslowpan_addr_context*
addr_context_lookup_by_number(uint8_t number)
{
    /* Remove code to avoid warnings and save flash if no context is used */
#if SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS > 0
    int i;
    for (i = 0; i < SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS; i++)
    {
        if ((addr_contexts[i].used == 1) && addr_contexts[i].number == number)
        {
            return &addr_contexts[i];
        }
    }
#endif /* SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS > 0 */
    return NULL;
}
/*--------------------------------------------------------------------*/
static uint8_t compress_addr_64(uint8_t bitpos, uip_ipaddr_t *ipaddr,
                                uip_lladdr_t *lladdr)
{
    if (uip_is_addr_mac_addr_based(ipaddr, lladdr))
    {
        return 3 << bitpos; /* 0-bits */
    }
    else if (sicslowpan_is_iid_16_bit_compressable(ipaddr))
    {
        /* compress IID to 16 bits xxxx::0000:00ff:fe00:XXXX */
        memcpy(hc06_ptr, &ipaddr->u16[7], 2);
        hc06_ptr += 2;
        return 2 << bitpos; /* 16-bits */
    }
    else
    {
        /* do not compress IID => xxxx::IID */
        memcpy(hc06_ptr, &ipaddr->u16[4], 8);
        hc06_ptr += 8;
        return 1 << bitpos; /* 64-bits */
    }
}

/*-------------------------------------------------------------------- */
/* Uncompress addresses based on a prefix and a postfix with zeroes in
 * between. If the postfix is zero in length it will use the link address
 * to configure the IP address (autoconf style).
 * pref_post_count takes a byte where the first nibble specify prefix count
 * and the second postfix count (NOTE: 15/0xf => 16 bytes copy).
 */
static void uncompress_addr(uip_ipaddr_t *ipaddr, uint8_t const prefix[],
                            uint8_t pref_post_count, uip_lladdr_t *lladdr)
{
    uint8_t prefcount = pref_post_count >> 4;
    uint8_t postcount = pref_post_count & 0x0f;
    /* full nibble 15 => 16 */
    prefcount = prefcount == 15 ? 16 : prefcount;
    postcount = postcount == 15 ? 16 : postcount;

    PRINTF("Uncompressing %d + %d => ", prefcount, postcount);

    if (prefcount > 0)
    {
        memcpy(ipaddr, prefix, prefcount);
    }
    if (prefcount + postcount < 16)
    {
        memset(&ipaddr->u8[prefcount], 0, 16 - (prefcount + postcount));
    }
    if (postcount > 0)
    {
        memcpy(&ipaddr->u8[16 - postcount], hc06_ptr, postcount);
        if (postcount == 2 && prefcount < 11)
        {
            /* 16 bits uncompression => 0000:00ff:fe00:XXXX */
            ipaddr->u8[11] = 0xff;
            ipaddr->u8[12] = 0xfe;
        }
        hc06_ptr += postcount;
    }
    else if (prefcount > 0)
    {
        /* no IID based configuration if no prefix and no data => unspec */

        uip_ds6_set_addr_iid(ipaddr, lladdr);

    }

    PRINT6ADDR(ipaddr); PRINTF("\n");
}

/*--------------------------------------------------------------------*/
/**
 * \brief Compress IP/UDP header
 *
 * This function is called by the 6lowpan code to create a compressed
 * 6lowpan packet in the packetbuf buffer from a full IPv6 packet in the
 * uip_buf buffer.
 *
 *
 * HC-06 (draft-ietf-6lowpan-hc, version 6)\n
 * http://tools.ietf.org/html/draft-ietf-6lowpan-hc-06
 *
 * \note We do not support ISA100_UDP header compression
 *
 * For LOWPAN_UDP compression, we either compress both ports or none.
 * General format with LOWPAN_UDP compression is
 * \verbatim
 *                      1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |0|1|1|TF |N|HLI|C|S|SAM|M|D|DAM| SCI   | DCI   | comp. IPv6 hdr|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | compressed IPv6 fields .....                                  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | LOWPAN_UDP    | non compressed UDP fields ...                 |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | L4 data ...                                                   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * \endverbatim
 * \note The context number 00 is reserved for the link local prefix.
 * For unicast addresses, if we cannot compress the prefix, we neither
 * compress the IID.
 * \param rime_destaddr L2 destination address, needed to compress IP
 * dest
 */
static void compress_hdr_hc06(rimeaddr_t *rime_destaddr)
{
    uint8_t tmp, iphc0, iphc1;
#if 0 //DEBUG
    {   uint16_t ndx;
        PRINTF("before compression (%d): ", UIP_IP_BUF->len[1]);
        for(ndx = 0; ndx < UIP_IP_BUF->len[1] + 40; ndx++)
        {
            uint8_t data = ((uint8_t *) (UIP_IP_BUF))[ndx];
            PRINTF("%02x", data);
        }
        PRINTF("\n");
    }
#endif

    hc06_ptr = rime_ptr + 2;
    /*
     * As we copy some bit-length fields, in the IPHC encoding bytes,
     * we sometimes use |=
     * If the field is 0, and the current bit value in memory is 1,
     * this does not work. We therefore reset the IPHC encoding here
     */

    iphc0 = SICSLOWPAN_DISPATCH_IPHC;
    iphc1 = 0;
    RIME_IPHC_BUF[2] = 0; /* might not be used - but needs to be cleared */

    /*
     * Address handling needs to be made first since it might
     * cause an extra byte with [ SCI | DCI ]
     *
     */

    /* check if dest context exists (for allocating third byte) */
    /* TODO: fix this so that it remembers the looked up values for
     avoiding two lookups - or set the lookup values immediately */
    if (addr_context_lookup_by_prefix(&UIP_IP_BUF->destipaddr) != NULL
            || addr_context_lookup_by_prefix(&UIP_IP_BUF->srcipaddr) != NULL)
    {
        /* set context flag and increase hc06_ptr */
        PRINTF("IPHC: compressing dest or src ipaddr - setting CID\n");
        iphc1 |= SICSLOWPAN_IPHC_CID;
        hc06_ptr++;
    }

    /*
     * Traffic class, flow label
     * If flow label is 0, compress it. If traffic class is 0, compress it
     * We have to process both in the same time as the offset of traffic class
     * depends on the presence of version and flow label
     */

    /* hc06 format of tc is ECN | DSCP , original is DSCP | ECN */
    tmp = (UIP_IP_BUF->vtc << 4) | (UIP_IP_BUF->tcflow >> 4);
    tmp = ((tmp & 0x03) << 6) | (tmp >> 2);

    if (((UIP_IP_BUF->tcflow & 0x0F) == 0) && (UIP_IP_BUF->flow == 0))
    {
        /* flow label can be compressed */
        iphc0 |= SICSLOWPAN_IPHC_FL_C;
        if (((UIP_IP_BUF->vtc & 0x0F) == 0)
                && ((UIP_IP_BUF->tcflow & 0xF0) == 0))
        {
            /* compress (elide) all */
            iphc0 |= SICSLOWPAN_IPHC_TC_C;
        }
        else
        {
            /* compress only the flow label */
            *hc06_ptr = tmp;
            hc06_ptr += 1;
        }
    }
    else
    {
        /* Flow label cannot be compressed */
        if (((UIP_IP_BUF->vtc & 0x0F) == 0)
                && ((UIP_IP_BUF->tcflow & 0xF0) == 0))
        {
            /* compress only traffic class */
            iphc0 |= SICSLOWPAN_IPHC_TC_C;
            *hc06_ptr = (tmp & 0xc0) | (UIP_IP_BUF->tcflow & 0x0F);
            memcpy(hc06_ptr + 1, &UIP_IP_BUF->flow, 2);
            hc06_ptr += 3;
        }
        else
        {
            /* compress nothing */
            //memcpy(hc06_ptr, &UIP_IP_BUF ->vtc, 4);
            hc06_ptr[0] = UIP_IP_BUF->vtc;
            hc06_ptr[1] = UIP_IP_BUF->tcflow;
            hc06_ptr[2] = UIP_IP_BUF->flow >> 8;
            hc06_ptr[3] = UIP_IP_BUF->flow & 0xFF;
            /* but replace the top byte with the new ECN | DSCP format*/
            *hc06_ptr = tmp;
            hc06_ptr += 4;
        }
    }

    /* Note that the payload length is always compressed */

    /* Next header. We compress it if UDP */
#if UIP_CONF_UDP || UIP_CONF_ROUTER
    if (UIP_IP_BUF->proto == UIP_PROTO_UDP)
    {
        iphc0 |= SICSLOWPAN_IPHC_NH_C;
    }
#endif /*UIP_CONF_UDP*/
#ifdef SICSLOWPAN_NH_COMPRESSOR
    if(SICSLOWPAN_NH_COMPRESSOR.is_compressable(UIP_IP_BUF->proto))
    {
        iphc0 |= SICSLOWPAN_IPHC_NH_C;
    }
#endif
    if ((iphc0 & SICSLOWPAN_IPHC_NH_C) == 0)
    {
        *hc06_ptr = UIP_IP_BUF->proto;
        hc06_ptr += 1;
    }

    /*
     * Hop limit
     * if 1: compress, encoding is 01
     * if 64: compress, encoding is 10
     * if 255: compress, encoding is 11
     * else do not compress
     */
    switch (UIP_IP_BUF->ttl)
    {
    case 1:
        iphc0 |= SICSLOWPAN_IPHC_TTL_1;
        break;
    case 64:
        iphc0 |= SICSLOWPAN_IPHC_TTL_64;
        break;
    case 255:
        iphc0 |= SICSLOWPAN_IPHC_TTL_255;
        break;
    default:
        *hc06_ptr = UIP_IP_BUF->ttl;
        hc06_ptr += 1;
        break;
    }

    /* source address - cannot be multicast */
    if (uip_is_addr_unspecified(&UIP_IP_BUF->srcipaddr))
    {
        PRINTF("IPHC: compressing unspecified - setting SAC\n");
        iphc1 |= SICSLOWPAN_IPHC_SAC;
        iphc1 |= SICSLOWPAN_IPHC_SAM_00;
    }
    else if ((context = addr_context_lookup_by_prefix(&UIP_IP_BUF->srcipaddr))
            != NULL)
    {
        /* elide the prefix - indicate by CID and set context + SAC */
        PRINTF("IPHC: compressing src with context - setting CID & SAC ctx: %d\n",
                context->number);
        iphc1 |= SICSLOWPAN_IPHC_CID | SICSLOWPAN_IPHC_SAC;
        RIME_IPHC_BUF[2] |= context->number << 4;
        /* compession compare with this nodes address (source) */

        iphc1 |= compress_addr_64(SICSLOWPAN_IPHC_SAM_BIT,
                                  &UIP_IP_BUF->srcipaddr, &uip_lladdr);
        /* No context found for this address */
    }
    else if (uip_is_addr_link_local(&UIP_IP_BUF->srcipaddr) &&
    UIP_IP_BUF->destipaddr.u16[1] == 0 &&
    UIP_IP_BUF->destipaddr.u16[2] == 0 &&
    UIP_IP_BUF->destipaddr.u16[3] == 0)
    {
        iphc1 |= compress_addr_64(SICSLOWPAN_IPHC_SAM_BIT,
                                  &UIP_IP_BUF->srcipaddr, &uip_lladdr);
    }
    else
    {
        /* send the full address => SAC = 0, SAM = 00 */
        iphc1 |= SICSLOWPAN_IPHC_SAM_00; /* 128-bits */
        memcpy(hc06_ptr, &UIP_IP_BUF->srcipaddr.u16[0], 16);
        hc06_ptr += 16;
    }

    /* dest address*/
    if (uip_is_addr_mcast(&UIP_IP_BUF->destipaddr))
    {
        /* Address is multicast, try to compress */
        iphc1 |= SICSLOWPAN_IPHC_M;
        if (sicslowpan_is_mcast_addr_compressable8(&UIP_IP_BUF->destipaddr))
        {
            iphc1 |= SICSLOWPAN_IPHC_DAM_11;
            /* use last byte */
            *hc06_ptr = UIP_IP_BUF->destipaddr.u8[15];
            hc06_ptr += 1;
        }
        else if (sicslowpan_is_mcast_addr_compressable32(
                &UIP_IP_BUF->destipaddr))
        {
            iphc1 |= SICSLOWPAN_IPHC_DAM_10;
            /* second byte + the last three */
            *hc06_ptr = UIP_IP_BUF->destipaddr.u8[1];
            memcpy(hc06_ptr + 1, &UIP_IP_BUF->destipaddr.u8[13], 3);
            hc06_ptr += 4;
        }
        else if (sicslowpan_is_mcast_addr_compressable48(
                &UIP_IP_BUF->destipaddr))
        {
            iphc1 |= SICSLOWPAN_IPHC_DAM_01;
            /* second byte + the last five */
            *hc06_ptr = UIP_IP_BUF->destipaddr.u8[1];
            memcpy(hc06_ptr + 1, &UIP_IP_BUF->destipaddr.u8[11], 5);
            hc06_ptr += 6;
        }
        else
        {
            iphc1 |= SICSLOWPAN_IPHC_DAM_00;
            /* full address */
            memcpy(hc06_ptr, &UIP_IP_BUF->destipaddr.u8[0], 16);
            hc06_ptr += 16;
        }
    }
    else
    {
        /* Address is unicast, try to compress */
        if ((context = addr_context_lookup_by_prefix(&UIP_IP_BUF->destipaddr))
                != NULL)
        {
            /* elide the prefix */
            iphc1 |= SICSLOWPAN_IPHC_DAC;
            RIME_IPHC_BUF[2] |= context->number;
            /* compession compare with link adress (destination) */

            iphc1 |= compress_addr_64(SICSLOWPAN_IPHC_DAM_BIT,
                                      &UIP_IP_BUF->destipaddr,
                                      (uip_lladdr_t *) rime_destaddr);
            /* No context found for this address */
        }
        else if (uip_is_addr_link_local(&UIP_IP_BUF->destipaddr) &&
        UIP_IP_BUF->destipaddr.u16[1] == 0 &&
        UIP_IP_BUF->destipaddr.u16[2] == 0 &&
        UIP_IP_BUF->destipaddr.u16[3] == 0)
        {
            iphc1 |= compress_addr_64(SICSLOWPAN_IPHC_DAM_BIT,
                                      &UIP_IP_BUF->destipaddr,
                                      (uip_lladdr_t *) rime_destaddr);
        }
        else
        {
            /* send the full address */
            iphc1 |= SICSLOWPAN_IPHC_DAM_00; /* 128-bits */
            memcpy(hc06_ptr, &UIP_IP_BUF->destipaddr.u16[0], 16);
            hc06_ptr += 16;
        }
    }

    uncomp_hdr_len = UIP_IPH_LEN;

#if UIP_CONF_UDP || UIP_CONF_ROUTER
    /* UDP header compression */
    if (UIP_IP_BUF->proto == UIP_PROTO_UDP)
    {
        PRINTF("IPHC: Uncompressed UDP ports on send side: %x, %x\n",
                UIP_HTONS(UIP_UDP_BUF->srcport), UIP_HTONS(UIP_UDP_BUF->destport));
        /* Mask out the last 4 bits can be used as a mask */
        if (((UIP_HTONS(UIP_UDP_BUF->srcport) & 0xfff0)
                == SICSLOWPAN_UDP_4_BIT_PORT_MIN)
                && ((UIP_HTONS(UIP_UDP_BUF->destport) & 0xfff0)
                        == SICSLOWPAN_UDP_4_BIT_PORT_MIN))
        {
            /* we can compress 12 bits of both source and dest */
            *hc06_ptr = SICSLOWPAN_NHC_UDP_CS_P_11;
            PRINTF("IPHC: remove 12 b of both source & dest with prefix 0xFOB\n");
            *(hc06_ptr + 1) = (uint8_t) ((UIP_HTONS(UIP_UDP_BUF->srcport) -
            SICSLOWPAN_UDP_4_BIT_PORT_MIN) << 4)
                    + (uint8_t) ((UIP_HTONS(UIP_UDP_BUF->destport) -
                    SICSLOWPAN_UDP_4_BIT_PORT_MIN));
            hc06_ptr += 2;
        }
        else if ((UIP_HTONS(UIP_UDP_BUF->destport) & 0xff00)
                == SICSLOWPAN_UDP_8_BIT_PORT_MIN)
        {
            /* we can compress 8 bits of dest, leave source. */
            *hc06_ptr = SICSLOWPAN_NHC_UDP_CS_P_01;
            PRINTF("IPHC: leave source, remove 8 bits of dest with prefix 0xF0\n");
            memcpy(hc06_ptr + 1, &UIP_UDP_BUF->srcport, 2);
            *(hc06_ptr + 3) = (uint8_t) ((UIP_HTONS(UIP_UDP_BUF->destport) -
            SICSLOWPAN_UDP_8_BIT_PORT_MIN));
            hc06_ptr += 4;
        }
        else if ((UIP_HTONS(UIP_UDP_BUF->srcport) & 0xff00)
                == SICSLOWPAN_UDP_8_BIT_PORT_MIN)
        {
            /* we can compress 8 bits of src, leave dest. Copy compressed port */
            *hc06_ptr = SICSLOWPAN_NHC_UDP_CS_P_10;
            PRINTF("IPHC: remove 8 bits of source with prefix 0xF0, leave dest. hch: %i\n", *hc06_ptr);
            *(hc06_ptr + 1) = (uint8_t) ((UIP_HTONS(UIP_UDP_BUF->srcport) -
            SICSLOWPAN_UDP_8_BIT_PORT_MIN));
            memcpy(hc06_ptr + 2, &UIP_UDP_BUF->destport, 2);
            hc06_ptr += 4;
        }
        else
        {
            /* we cannot compress. Copy uncompressed ports, full checksum  */
            *hc06_ptr = SICSLOWPAN_NHC_UDP_CS_P_00;
            PRINTF("IPHC: cannot compress headers\n");
            memcpy(hc06_ptr + 1, &UIP_UDP_BUF->srcport, 4);
            hc06_ptr += 5;
        }
        /* always inline the checksum  */
        if (1)
        {
            memcpy(hc06_ptr, &UIP_UDP_BUF->udpchksum, 2);
            hc06_ptr += 2;
        }
        uncomp_hdr_len += UIP_UDPH_LEN;
    }
#endif /*UIP_CONF_UDP*/

#ifdef SICSLOWPAN_NH_COMPRESSOR
    /* if nothing to compress just return zero  */
    hc06_ptr += SICSLOWPAN_NH_COMPRESSOR.compress(hc06_ptr, &uncomp_hdr_len);
#endif

    /* before the rime_hdr_len operation */
    RIME_IPHC_BUF[0] = iphc0;
    RIME_IPHC_BUF[1] = iphc1;

    rime_hdr_len = hc06_ptr - rime_ptr;
    return;
}

/*--------------------------------------------------------------------*/
/**
 * \brief Uncompress HC06 (i.e., IPHC and LOWPAN_UDP) headers and put
 * them in sicslowpan_buf
 *
 * This function is called by the input function when the dispatch is
 * HC06.
 * We %process the packet in the rime buffer, uncompress the header
 * fields, and copy the result in the sicslowpan buffer.
 * At the end of the decompression, rime_hdr_len and uncompressed_hdr_len
 * are set to the appropriate values
 *
 * \param ip_len Equal to 0 if the packet is not a fragment (IP length
 * is then inferred from the L2 length), non 0 if the packet is a 1st
 * fragment.
 */
static void uncompress_hdr_hc06(uint16_t ip_len)
{
    uint8_t tmp, iphc0, iphc1;
    /* at least two byte will be used for the encoding */
    hc06_ptr = rime_ptr + rime_hdr_len + 2;

    iphc0 = RIME_IPHC_BUF[0];
    iphc1 = RIME_IPHC_BUF[1];

    /* another if the CID flag is set */
    if (iphc1 & SICSLOWPAN_IPHC_CID)
    {
        PRINTF("IPHC: CID flag set - increase header with one\n");
        hc06_ptr++;
    }

    /* Traffic class and flow label */
    if ((iphc0 & SICSLOWPAN_IPHC_FL_C) == 0)
    {
        /* Flow label are carried inline */
        if ((iphc0 & SICSLOWPAN_IPHC_TC_C) == 0)
        {
            /* Traffic class is carried inline */
            //memcpy(&SICSLOWPAN_IP_BUF ->tcflow, hc06_ptr + 1, 3);
            SICSLOWPAN_IP_BUF->tcflow = hc06_ptr[1];
            SICSLOWPAN_IP_BUF->flow = UIP_HTONS(
                    (hc06_ptr[2] << 8) + hc06_ptr[3]);
            tmp = *hc06_ptr;
            hc06_ptr += 4;
            /* hc06 format of tc is ECN | DSCP , original is DSCP | ECN */
            /* set version, pick highest DSCP bits and set in vtc */
            SICSLOWPAN_IP_BUF->vtc = 0x60 | ((tmp >> 2) & 0x0f);
            /* ECN rolled down two steps + lowest DSCP bits at top two bits */
            SICSLOWPAN_IP_BUF->tcflow = ((tmp >> 2) & 0x30) | (tmp << 6)
                    | (SICSLOWPAN_IP_BUF->tcflow & 0x0f);
        }
        else
        {
            /* Traffic class is compressed (set version and no TC)*/
            SICSLOWPAN_IP_BUF->vtc = 0x60;
            /* highest flow label bits + ECN bits */
            SICSLOWPAN_IP_BUF->tcflow = (*hc06_ptr & 0x0F)
                    | ((*hc06_ptr >> 2) & 0x30);
            memcpy(&SICSLOWPAN_IP_BUF->flow, hc06_ptr + 1, 2);
            hc06_ptr += 3;
        }
    }
    else
    {
        /* Version is always 6! */
        /* Version and flow label are compressed */
        if ((iphc0 & SICSLOWPAN_IPHC_TC_C) == 0)
        {
            /* Traffic class is inline */
            SICSLOWPAN_IP_BUF->vtc = 0x60 | ((*hc06_ptr >> 2) & 0x0f);
            SICSLOWPAN_IP_BUF->tcflow = ((*hc06_ptr << 6) & 0xC0)
                    | ((*hc06_ptr >> 2) & 0x30);
            SICSLOWPAN_IP_BUF->flow = 0;
            hc06_ptr += 1;
        }
        else
        {
            /* Traffic class is compressed */
            SICSLOWPAN_IP_BUF->vtc = 0x60;
            SICSLOWPAN_IP_BUF->tcflow = 0;
            SICSLOWPAN_IP_BUF->flow = 0;
        }
    }

    /* Next Header */
    if ((iphc0 & SICSLOWPAN_IPHC_NH_C) == 0)
    {
        /* Next header is carried inline */
        SICSLOWPAN_IP_BUF->proto = *hc06_ptr;
        PRINTF("IPHC: next header inline: %d\n", SICSLOWPAN_IP_BUF->proto);
        hc06_ptr += 1;
    }

    /* Hop limit */
    if ((iphc0 & 0x03) != SICSLOWPAN_IPHC_TTL_I)
    {
        SICSLOWPAN_IP_BUF->ttl = ttl_values[iphc0 & 0x03];
    }
    else
    {
        SICSLOWPAN_IP_BUF->ttl = *hc06_ptr;
        hc06_ptr += 1;
    }

    /* put the source address compression mode SAM in the tmp var */
    tmp = ((iphc1 & SICSLOWPAN_IPHC_SAM_11) >> SICSLOWPAN_IPHC_SAM_BIT) & 0x03;

    /* context based compression */
    if (iphc1 & SICSLOWPAN_IPHC_SAC)
    {
        uint8_t sci = (iphc1 & SICSLOWPAN_IPHC_CID) ?
        RIME_IPHC_BUF[2] >> 4 :
                                                      0;

        /* Source address - check context != NULL only if SAM bits are != 0*/
        if (tmp != 0)
        {
            context = addr_context_lookup_by_number(sci);
            if (context == NULL)
            {
                PRINTF("sicslowpan uncompress_hdr: error context not found\n");
                return;
            }
        }
        /* if tmp == 0 we do not have a context and therefore no prefix */
        uncompress_addr(&SICSLOWPAN_IP_BUF->srcipaddr,
                        tmp != 0 ? context->prefix : NULL, unc_ctxconf[tmp],
                        (uip_lladdr_t *) packetbuf_addr(PACKETBUF_ADDR_SENDER));
    }
    else
    {
        /* no compression and link local */
        uncompress_addr(&SICSLOWPAN_IP_BUF->srcipaddr, llprefix,
                        unc_llconf[tmp],
                        (uip_lladdr_t *) packetbuf_addr(PACKETBUF_ADDR_SENDER));
    }

    /* Destination address */
    /* put the destination address compression mode into tmp */
    tmp = ((iphc1 & SICSLOWPAN_IPHC_DAM_11) >> SICSLOWPAN_IPHC_DAM_BIT) & 0x03;

    /* multicast compression */
    if (iphc1 & SICSLOWPAN_IPHC_M)
    {
        /* context based multicast compression */
        if (iphc1 & SICSLOWPAN_IPHC_DAC)
        {
            /* TODO: implement this */
        }
        else
        {
            /* non-context based multicast compression - */
            /* DAM_00: 128 bits  */
            /* DAM_01:  48 bits FFXX::00XX:XXXX:XXXX */
            /* DAM_10:  32 bits FFXX::00XX:XXXX */
            /* DAM_11:   8 bits FF02::00XX */
            uint8_t prefix[] = { 0xff, 0x02 };
            if (tmp > 0 && tmp < 3)
            {
                prefix[1] = *hc06_ptr;
                hc06_ptr++;
            }

            uncompress_addr(&SICSLOWPAN_IP_BUF->destipaddr, prefix,
                            unc_mxconf[tmp], NULL);
        }
    }
    else
    {
        /* no multicast */
        /* Context based */
        if (iphc1 & SICSLOWPAN_IPHC_DAC)
        {
            uint8_t dci = (iphc1 & SICSLOWPAN_IPHC_CID) ?
            RIME_IPHC_BUF[2] & 0x0f :
                                                          0;
            context = addr_context_lookup_by_number(dci);

            /* all valid cases below need the context! */
            if (context == NULL)
            {
                PRINTF("sicslowpan uncompress_hdr: error context not found\n");
                return;
            }
            uncompress_addr(
                    &SICSLOWPAN_IP_BUF->destipaddr, context->prefix,
                    unc_ctxconf[tmp],
                    (uip_lladdr_t *) packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
        }
        else
        {
            /* not context based => link local M = 0, DAC = 0 - same as SAC */
            uncompress_addr(
                    &SICSLOWPAN_IP_BUF->destipaddr, llprefix, unc_llconf[tmp],
                    (uip_lladdr_t *) packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
        }
    }
    uncomp_hdr_len += UIP_IPH_LEN;

    /* Next header processing - continued */
    if ((iphc0 & SICSLOWPAN_IPHC_NH_C))
    {
        /* The next header is compressed, NHC is following */
        if ((*hc06_ptr & SICSLOWPAN_NHC_UDP_MASK) == SICSLOWPAN_NHC_UDP_ID)
        {
            uint8_t checksum_compressed;
            SICSLOWPAN_IP_BUF->proto = UIP_PROTO_UDP;
            checksum_compressed = *hc06_ptr & SICSLOWPAN_NHC_UDP_CHECKSUMC;
            PRINTF("IPHC: Incoming header value: %i\n", *hc06_ptr);
            switch (*hc06_ptr & SICSLOWPAN_NHC_UDP_CS_P_11)
            {
            case SICSLOWPAN_NHC_UDP_CS_P_00:
                /* 1 byte for NHC, 4 byte for ports, 2 bytes chksum */
                memcpy(&SICSLOWPAN_UDP_BUF->srcport, hc06_ptr + 1, 2);
                memcpy(&SICSLOWPAN_UDP_BUF->destport, hc06_ptr + 3, 2);
                PRINTF("IPHC: Uncompressed UDP ports (ptr+5): %x, %x\n",
                        UIP_HTONS(SICSLOWPAN_UDP_BUF->srcport), UIP_HTONS(SICSLOWPAN_UDP_BUF->destport));
                hc06_ptr += 5;
                break;

            case SICSLOWPAN_NHC_UDP_CS_P_01:
                /* 1 byte for NHC + source 16bit inline, dest = 0xF0 + 8 bit inline */
                PRINTF("IPHC: Decompressing destination\n");
                memcpy(&SICSLOWPAN_UDP_BUF->srcport, hc06_ptr + 1, 2);
                SICSLOWPAN_UDP_BUF->destport = UIP_HTONS(
                        SICSLOWPAN_UDP_8_BIT_PORT_MIN + (*(hc06_ptr + 3)));
                PRINTF("IPHC: Uncompressed UDP ports (ptr+4): %x, %x\n",
                        UIP_HTONS(SICSLOWPAN_UDP_BUF->srcport), UIP_HTONS(SICSLOWPAN_UDP_BUF->destport));
                hc06_ptr += 4;
                break;

            case SICSLOWPAN_NHC_UDP_CS_P_10:
                /* 1 byte for NHC + source = 0xF0 + 8bit inline, dest = 16 bit inline*/
                PRINTF("IPHC: Decompressing source\n");
                SICSLOWPAN_UDP_BUF->srcport = UIP_HTONS(
                        SICSLOWPAN_UDP_8_BIT_PORT_MIN + (*(hc06_ptr + 1)));
                memcpy(&SICSLOWPAN_UDP_BUF->destport, hc06_ptr + 2, 2);
                PRINTF("IPHC: Uncompressed UDP ports (ptr+4): %x, %x\n",
                        UIP_HTONS(SICSLOWPAN_UDP_BUF->srcport), UIP_HTONS(SICSLOWPAN_UDP_BUF->destport));
                hc06_ptr += 4;
                break;

            case SICSLOWPAN_NHC_UDP_CS_P_11:
                /* 1 byte for NHC, 1 byte for ports */
                SICSLOWPAN_UDP_BUF->srcport = UIP_HTONS(
                        SICSLOWPAN_UDP_4_BIT_PORT_MIN + (*(hc06_ptr + 1) >> 4));
                SICSLOWPAN_UDP_BUF->destport =
                        UIP_HTONS(
                                SICSLOWPAN_UDP_4_BIT_PORT_MIN + ((*(hc06_ptr + 1)) & 0x0F));
                PRINTF("IPHC: Uncompressed UDP ports (ptr+2): %x, %x\n",
                        UIP_HTONS(SICSLOWPAN_UDP_BUF->srcport), UIP_HTONS(SICSLOWPAN_UDP_BUF->destport));
                hc06_ptr += 2;
                break;

            default:
                PRINTF("sicslowpan uncompress_hdr: error unsupported UDP compression\n");
                return;
            }
            if (!checksum_compressed)
            { /* has_checksum, default  */
                memcpy(&SICSLOWPAN_UDP_BUF->udpchksum, hc06_ptr, 2);
                hc06_ptr += 2;
                PRINTF("IPHC: sicslowpan uncompress_hdr: checksum included\n");
            }
            else
            {
                PRINTF("IPHC: sicslowpan uncompress_hdr: checksum *NOT* included\n");
            }
            uncomp_hdr_len += UIP_UDPH_LEN;
        }
#ifdef SICSLOWPAN_NH_COMPRESSOR
        else
        {
            hc06_ptr += SICSLOWPAN_NH_COMPRESSOR.uncompress(hc06_ptr, sicslowpan_buf, &uncomp_hdr_len);
        }
#endif
    }

    rime_hdr_len = hc06_ptr - rime_ptr;

    /* IP length field. */
    if (ip_len == 0)
    {
        /* This is not a fragmented packet */
        SICSLOWPAN_IP_BUF->len0 = (packetbuf_datalen_read() - rime_hdr_len
                + uncomp_hdr_len - UIP_IPH_LEN) >> 8;
        //SICSLOWPAN_IP_BUF->len1 = packetbuf_datalen() - rime_hdr_len + uncomp_hdr_len - UIP_IPH_LEN;
        SICSLOWPAN_IP_BUF->len1 = (packetbuf_datalen_read() - rime_hdr_len
                + uncomp_hdr_len - UIP_IPH_LEN) & 0x00FF;
    }
    else
    {
        /* This is a 1st fragment */
        SICSLOWPAN_IP_BUF->len0 = (ip_len - UIP_IPH_LEN) >> 8;
        SICSLOWPAN_IP_BUF->len1 = (ip_len - UIP_IPH_LEN) & 0x00FF;
    }

    /* length field in UDP header */
    if (SICSLOWPAN_IP_BUF->proto == UIP_PROTO_UDP)
    {
        //memcpy(&SICSLOWPAN_UDP_BUF ->udplen, &SICSLOWPAN_IP_BUF ->len[0], 2);
        SICSLOWPAN_UDP_BUF->udplen = UIP_HTONS(
                (SICSLOWPAN_IP_BUF->len0 << 8) + SICSLOWPAN_IP_BUF->len1);
    }

    return;
}
/** @} */
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06 */

#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC1
/*--------------------------------------------------------------------*/
/** \name HC1 compression and uncompression functions
 *  @{                                                                */
/*--------------------------------------------------------------------*/
/**
 * \brief Compress IP/UDP header using HC1 and HC_UDP
 *
 * This function is called by the 6lowpan code to create a compressed
 * 6lowpan packet in the packetbuf buffer from a full IPv6 packet in the
 * uip_buf buffer.
 *
 *
 * If we can compress everything, we use HC1 dispatch, if not we use
 * IPv6 dispatch.\n
 * We can compress everything if:
 *   - IP version is
 *   - Flow label and traffic class are 0
 *   - Both src and dest ip addresses are link local
 *   - Both src and dest interface ID are recoverable from lower layer
 *     header
 *   - Next header is either ICMP, UDP or TCP
 * Moreover, if next header is UDP, we try to compress it using HC_UDP.
 * This is feasible is both ports are between F0B0 and F0B0 + 15\n\n
 *
 * Resulting header structure:
 * - For ICMP, TCP, non compressed UDP\n
 *   HC1 encoding = 11111010 (UDP) 11111110 (TCP) 11111100 (ICMP)\n
 * \verbatim
 *                      1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | LoWPAN HC1 Dsp | HC1 encoding  | IPv6 Hop limit| L4 hdr + data|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * \endverbatim
 *
 * - For compressed UDP
 *   HC1 encoding = 11111011, HC_UDP encoding = 11100000\n
 * \verbatim
 *                      1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | LoWPAN HC1 Dsp| HC1 encoding  |  HC_UDP encod.| IPv6 Hop limit|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | src p.| dst p.| UDP checksum                  | L4 data...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * \endverbatim
 *
 * \param rime_destaddr L2 destination address, needed to compress the
 * IP destination field
 */
static void
compress_hdr_hc1(rimeaddr_t *rime_destaddr)
{
    /*
     * Check if all the assumptions for full compression
     * are valid :
     */
    if(UIP_IP_BUF->vtc != 0x60 ||
            UIP_IP_BUF->tcflow != 0 ||
            UIP_IP_BUF->flow != 0 ||
            !uip_is_addr_link_local(&UIP_IP_BUF->srcipaddr) ||
            !uip_is_addr_mac_addr_based(&UIP_IP_BUF->srcipaddr, &uip_lladdr) ||
            !uip_is_addr_link_local(&UIP_IP_BUF->destipaddr) ||
            !uip_is_addr_mac_addr_based(&UIP_IP_BUF->destipaddr,
                    (uip_lladdr_t *)rime_destaddr) ||
            (UIP_IP_BUF->proto != UIP_PROTO_ICMP6 &&
                    UIP_IP_BUF->proto != UIP_PROTO_UDP &&
                    UIP_IP_BUF->proto != UIP_PROTO_TCP))
    {
        /*
         * IPV6 DISPATCH
         * Something cannot be compressed, use IPV6 DISPATCH,
         * compress nothing, copy IPv6 header in rime buffer
         */
        *rime_ptr = SICSLOWPAN_DISPATCH_IPV6;
        rime_hdr_len += SICSLOWPAN_IPV6_HDR_LEN;
        memcpy(rime_ptr + rime_hdr_len, UIP_IP_BUF, UIP_IPH_LEN);
        rime_hdr_len += UIP_IPH_LEN;
        uncomp_hdr_len += UIP_IPH_LEN;
    }
    else
    {
        /*
         * HC1 DISPATCH
         * maximum compresssion:
         * All fields in the IP header but Hop Limit are elided
         * If next header is UDP, we compress UDP header using HC2
         */
        RIME_HC1_PTR[RIME_HC1_DISPATCH] = SICSLOWPAN_DISPATCH_HC1;
        uncomp_hdr_len += UIP_IPH_LEN;
        switch(UIP_IP_BUF->proto)
        {
            case UIP_PROTO_ICMP6:
            /* HC1 encoding and ttl */
            RIME_HC1_PTR[RIME_HC1_ENCODING] = 0xFC;
            RIME_HC1_PTR[RIME_HC1_TTL] = UIP_IP_BUF->ttl;
            rime_hdr_len += SICSLOWPAN_HC1_HDR_LEN;
            break;
#if UIP_CONF_TCP
            case UIP_PROTO_TCP:
            /* HC1 encoding and ttl */
            RIME_HC1_PTR[RIME_HC1_ENCODING] = 0xFE;
            RIME_HC1_PTR[RIME_HC1_TTL] = UIP_IP_BUF->ttl;
            rime_hdr_len += SICSLOWPAN_HC1_HDR_LEN;
            break;
#endif /* UIP_CONF_TCP */
#if UIP_CONF_UDP
            case UIP_PROTO_UDP:
            /*
             * try to compress UDP header (we do only full compression).
             * This is feasible if both src and dest ports are between
             * SICSLOWPAN_UDP_PORT_MIN and SICSLOWPAN_UDP_PORT_MIN + 15
             */
            PRINTF("local/remote port %u/%u\n",UIP_UDP_BUF->srcport,UIP_UDP_BUF->destport);
            if(UIP_HTONS(UIP_UDP_BUF->srcport) >= SICSLOWPAN_UDP_PORT_MIN &&
                    UIP_HTONS(UIP_UDP_BUF->srcport) < SICSLOWPAN_UDP_PORT_MAX &&
                    UIP_HTONS(UIP_UDP_BUF->destport) >= SICSLOWPAN_UDP_PORT_MIN &&
                    UIP_HTONS(UIP_UDP_BUF->destport) < SICSLOWPAN_UDP_PORT_MAX)
            {
                /* HC1 encoding */
                RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_HC1_ENCODING] = 0xFB;

                /* HC_UDP encoding, ttl, src and dest ports, checksum */
                RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_UDP_ENCODING] = 0xE0;
                RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_TTL] = UIP_IP_BUF->ttl;

                RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_PORTS] =
                (uint8_t)((UIP_HTONS(UIP_UDP_BUF->srcport) -
                                SICSLOWPAN_UDP_PORT_MIN) << 4) +
                (uint8_t)((UIP_HTONS(UIP_UDP_BUF->destport) - SICSLOWPAN_UDP_PORT_MIN));
                memcpy(&RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_CHKSUM], &UIP_UDP_BUF->udpchksum, 2);
                rime_hdr_len += SICSLOWPAN_HC1_HC_UDP_HDR_LEN;
                uncomp_hdr_len += UIP_UDPH_LEN;
            }
            else
            {
                /* HC1 encoding and ttl */
                RIME_HC1_PTR[RIME_HC1_ENCODING] = 0xFA;
                RIME_HC1_PTR[RIME_HC1_TTL] = UIP_IP_BUF->ttl;
                rime_hdr_len += SICSLOWPAN_HC1_HDR_LEN;
            }
            break;
#endif /*UIP_CONF_UDP*/
        }
    }
    return;
}

/*--------------------------------------------------------------------*/
/**
 * \brief Uncompress HC1 (and HC_UDP) headers and put them in
 * sicslowpan_buf
 *
 * This function is called by the input function when the dispatch is
 * HC1.
 * We %process the packet in the rime buffer, uncompress the header
 * fields, and copy the result in the sicslowpan buffer.
 * At the end of the decompression, rime_hdr_len and uncompressed_hdr_len
 * are set to the appropriate values
 *
 * \param ip_len Equal to 0 if the packet is not a fragment (IP length
 * is then inferred from the L2 length), non 0 if the packet is a 1st
 * fragment.
 */
static void
uncompress_hdr_hc1(uint16_t ip_len)
{
    /* version, traffic class, flow label */
    SICSLOWPAN_IP_BUF->vtc = 0x60;
    SICSLOWPAN_IP_BUF->tcflow = 0;
    SICSLOWPAN_IP_BUF->flow = 0;

    /* src and dest ip addresses */
    uip_ip6addr(&SICSLOWPAN_IP_BUF->srcipaddr, 0xfe80, 0, 0, 0, 0, 0, 0, 0);
    uip_ds6_set_addr_iid(&SICSLOWPAN_IP_BUF->srcipaddr,
            (uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER));
    uip_ip6addr(&SICSLOWPAN_IP_BUF->destipaddr, 0xfe80, 0, 0, 0, 0, 0, 0, 0);
    uip_ds6_set_addr_iid(&SICSLOWPAN_IP_BUF->destipaddr,
            (uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_RECEIVER));

    uncomp_hdr_len += UIP_IPH_LEN;

    /* Next header field */
    switch(RIME_HC1_PTR[RIME_HC1_ENCODING] & 0x06)
    {
        case SICSLOWPAN_HC1_NH_ICMP6:
        SICSLOWPAN_IP_BUF->proto = UIP_PROTO_ICMP6;
        SICSLOWPAN_IP_BUF->ttl = RIME_HC1_PTR[RIME_HC1_TTL];
        rime_hdr_len += SICSLOWPAN_HC1_HDR_LEN;
        break;
#if UIP_CONF_TCP
        case SICSLOWPAN_HC1_NH_TCP:
        SICSLOWPAN_IP_BUF->proto = UIP_PROTO_TCP;
        SICSLOWPAN_IP_BUF->ttl = RIME_HC1_PTR[RIME_HC1_TTL];
        rime_hdr_len += SICSLOWPAN_HC1_HDR_LEN;
        break;
#endif/* UIP_CONF_TCP */
#if UIP_CONF_UDP
        case SICSLOWPAN_HC1_NH_UDP:
        SICSLOWPAN_IP_BUF->proto = UIP_PROTO_UDP;
        if(RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_HC1_ENCODING] & 0x01)
        {
            /* UDP header is compressed with HC_UDP */
            if(RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_UDP_ENCODING] !=
                    SICSLOWPAN_HC_UDP_ALL_C)
            {
                PRINTF("sicslowpan (uncompress_hdr), packet not supported");
                return;
            }
            /* IP TTL */
            SICSLOWPAN_IP_BUF->ttl = RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_TTL];
            /* UDP ports, len, checksum */
            SICSLOWPAN_UDP_BUF->srcport =
            UIP_HTONS(SICSLOWPAN_UDP_PORT_MIN +
                    (RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_PORTS] >> 4));
            SICSLOWPAN_UDP_BUF->destport =
            UIP_HTONS(SICSLOWPAN_UDP_PORT_MIN +
                    (RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_PORTS] & 0x0F));
            memcpy(&SICSLOWPAN_UDP_BUF->udpchksum, &RIME_HC1_HC_UDP_PTR[RIME_HC1_HC_UDP_CHKSUM], 2);
            uncomp_hdr_len += UIP_UDPH_LEN;
            rime_hdr_len += SICSLOWPAN_HC1_HC_UDP_HDR_LEN;
        }
        else
        {
            rime_hdr_len += SICSLOWPAN_HC1_HDR_LEN;
        }
        break;
#endif/* UIP_CONF_UDP */
        default:
        /* this shouldn't happen, drop */
        return;
    }

    /* IP length field. */
    if(ip_len == 0)
    {
        /* This is not a fragmented packet */
        SICSLOWPAN_IP_BUF->len[0] = 0;
        SICSLOWPAN_IP_BUF->len[1] = packetbuf_datalen() - rime_hdr_len + uncomp_hdr_len - UIP_IPH_LEN;
    }
    else
    {
        /* This is a 1st fragment */
        SICSLOWPAN_IP_BUF->len[0] = (ip_len - UIP_IPH_LEN) >> 8;
        SICSLOWPAN_IP_BUF->len[1] = (ip_len - UIP_IPH_LEN) & 0x00FF;
    }
    /* length field in UDP header */
    if(SICSLOWPAN_IP_BUF->proto == UIP_PROTO_UDP)
    {
        memcpy(&SICSLOWPAN_UDP_BUF->udplen, &SICSLOWPAN_IP_BUF->len[0], 2);
    }
    return;
}
/** @} */
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC1 */

/*--------------------------------------------------------------------*/
/** \name IPv6 dispatch "compression" function
 * @{                                                                 */
/*--------------------------------------------------------------------*/
/* \brief Packets "Compression" when only IPv6 dispatch is used
 *
 * There is no compression in this case, all fields are sent
 * inline. We just add the IPv6 dispatch byte before the packet.
 * \verbatim
 * 0               1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | IPv6 Dsp      | IPv6 header and payload ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * \endverbatim
 */
static void compress_hdr_ipv6(rimeaddr_t *rime_destaddr)
{
    *rime_ptr = SICSLOWPAN_DISPATCH_IPV6;
    rime_hdr_len += SICSLOWPAN_IPV6_HDR_LEN;
    memcpy(rime_ptr + rime_hdr_len, UIP_IP_BUF, UIP_IPH_LEN);
    rime_hdr_len += UIP_IPH_LEN;
    uncomp_hdr_len += UIP_IPH_LEN;
    return;
}
/** @} */

/*--------------------------------------------------------------------*/
/** \name Input/output functions common to all compression schemes
 * @{                                                                 */
/*--------------------------------------------------------------------*/
/**
 * Callback function for the MAC packet sent callback
 */
static void packet_sent(void *ptr, int status, int transmissions)
{
    uip_ds6_link_neighbor_callback(status, transmissions);
    //last_tx_status = status;
}
/*--------------------------------------------------------------------*/
/**
 * \brief This function is called by the 6lowpan code to send out a
 * packet.
 * \param dest the link layer destination address of the packet
 */
#if !ORIGINAL_CONTIKI

static void send_packet(rimeaddr_t* dest)
{
    /* Set the link layer destination address for the packet as a
     * packetbuf attribute. The MAC layer can access the destination
     * address with the function packetbuf_addr(PACKETBUF_ADDR_RECEIVER).
     */
    packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, dest);

    //Use the transmission request to send to lower layer
    SICSLOWPAN_TX_REQUEST(dest);
}
#else

static void
send_packet(rimeaddr_t *dest)
{
    /* Set the link layer destination address for the packet as a
     * packetbuf attribute. The MAC layer can access the destination
     * address with the function packetbuf_addr(PACKETBUF_ADDR_RECEIVER).
     */
    packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, dest);

#if NETSTACK_CONF_BRIDGE_MODE
    /* This needs to be explicitly set here for bridge mode to work */
    packetbuf_set_addr(PACKETBUF_ADDR_SENDER,(void*)&uip_lladdr);
#endif

    /* Force acknowledge from sender (test hardware autoacks) */
#if SICSLOWPAN_CONF_ACK_ALL
    packetbuf_set_attr(PACKETBUF_ATTR_RELIABLE, 1);
#endif

    /* Provide a callback function to receive the result of
     a packet transmission. */
    NETSTACK_MAC.send(&packet_sent, NULL);

    /* If we are sending multiple packets in a row, we need to let the
     watchdog know that we are still alive. */
    //watchdog_periodic();
}
#endif
/*--------------------------------------------------------------------*/
/** \brief Take an IP packet and format it to be sent on an 802.15.4
 *  network using 6lowpan.
 *  \param localdest The MAC address of the destination
 *
 *  The IP packet is initially in uip_buf. Its header is compressed
 *  and if necessary it is fragmented. The resulting
 *  packet/fragments are put in packetbuf and delivered to the 802.15.4
 *  MAC.
 */
static uint16_t output(uip_lladdr_t *localdest)
{
    //int framer_hdrlen;

    /* The MAC address of the destination of the packet */
    rimeaddr_t dest;

    /* Number of bytes processed. */
    uint16_t processed_ip_out_len;

    UIP_STAT_SLP_TX_INC;

    /* init */
    uncomp_hdr_len = 0;
    rime_hdr_len = 0;

    if (uipPakcets[uipPackets_WriteIdx].buf_len != 0)
    {
        //Packet FIFO overflow
        lowpanTxPacketLossCount++;
    }
    else
    {
        /* reset rime buffer */
        uipPakcets_lock();

        packetbuf_clear();
        rime_ptr = packetbuf_dataptr();

        packetbuf_set_attr(PACKETBUF_ATTR_MAX_MAC_TRANSMISSIONS,
        SICSLOWPAN_MAX_MAC_TRANSMISSIONS);

#if UIP_CONF_TCP

#define TCP_FIN 0x01
#define TCP_ACK 0x10
#define TCP_CTL 0x3f
        /* Set stream mode for all TCP packets, except FIN packets. */
        if(UIP_IP_BUF->proto == UIP_PROTO_TCP &&
                (UIP_TCP_BUF->flags & TCP_FIN) == 0 &&
                (UIP_TCP_BUF->flags & TCP_CTL) != TCP_ACK)
        {
            packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE,
                    PACKETBUF_ATTR_PACKET_TYPE_STREAM);
        }
        else if(UIP_IP_BUF->proto == UIP_PROTO_TCP &&
                (UIP_TCP_BUF->flags & TCP_FIN) == TCP_FIN)
        {
            packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE,
                    PACKETBUF_ATTR_PACKET_TYPE_STREAM_END);
        }

#endif

        /*
         * The destination address will be tagged to each outbound
         * packet. If the argument localdest is NULL, we are sending a
         * broadcast packet.
         */
        if (localdest == NULL)
        {
            rimeaddr_copy(&dest, &rimeaddr_null);
        }
        else
        {
            rimeaddr_copy(&dest, (const rimeaddr_t *) localdest);
        }

        PRINTFO("sicslowpan output: sending packet len %d\n", uip_len);

        if (uip_len>= COMPRESSION_THRESHOLD)
        {
            /* Try to compress the headers */
#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC1
            compress_hdr_hc1(&dest);
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC1 */
#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_IPV6
            compress_hdr_ipv6(&dest);
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_IPV6 */
#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06
            compress_hdr_hc06(&dest);
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06 */
        }
        else
        {
            compress_hdr_ipv6(&dest);
        }
        PRINTFO("sicslowpan output: header of len %d\n", rime_hdr_len);

        /* Calculate NETSTACK_FRAMER's header length, that will be added in the NETSTACK_RDC.
         * We calculate it here only to make a better decision of whether the outgoing packet
         * needs to be fragmented or not. */
#define USE_FRAMER_HDRLEN 0
#if USE_FRAMER_HDRLEN
        packetbuf_clear();
        packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, &dest);
        framer_hdrlen = NETSTACK_FRAMER.create();
        if(framer_hdrlen < 0)
        {
            /* Framing failed, we assume the maximum header length */
            framer_hdrlen = 21;
        }
        packetbuf_clear();

        /* We must set the max transmissions attribute again after clearing
         the buffer. */
        packetbuf_set_attr(PACKETBUF_ATTR_MAX_MAC_TRANSMISSIONS,
                SICSLOWPAN_MAX_MAC_TRANSMISSIONS);
#else /* USE_FRAMER_HDRLEN */
        //framer_hdrlen = 21;
#endif /* USE_FRAMER_HDRLEN */

        //if((int)uip_len - (int)uncomp_hdr_len > (int)MAC_MAX_PAYLOAD - framer_hdrlen - (int)rime_hdr_len) {
        if (uip_len- uncomp_hdr_len > MAC_MAX_PAYLOAD - rime_hdr_len)
        {

#if SICSLOWPAN_CONF_FRAG
            struct queuebuf *q;
            /*
             * The outbound IPv6 packet is too large to fit into a single 15.4
             * packet, so we fragment it into multiple packets and send them.
             * The first fragment contains frag1 dispatch, then
             * IPv6/HC1/HC06/HC_UDP dispatchs/headers.
             * The following fragments contain only the fragn dispatch.
             */

            PRINTFO("Fragmentation sending packet len %d\n", uip_len);

            /* Create 1st Fragment */
            PRINTFO("sicslowpan output: 1st fragment ");

            /* move HC1/HC06/IPv6 header */
            memmove(rime_ptr + SICSLOWPAN_FRAG1_HDR_LEN, rime_ptr, rime_hdr_len);

            /*
             * FRAG1 dispatch + header
             * Note that the length is in units of 8 bytes
             */
            /*     RIME_FRAG_BUF->dispatch_size = */
            /*       uip_htons((SICSLOWPAN_DISPATCH_FRAG1 << 8) | uip_len); */
            SET16(RIME_FRAG_PTR, RIME_FRAG_DISPATCH_SIZE,
            ((SICSLOWPAN_DISPATCH_FRAG1 << 8) | uip_len));
            /*     RIME_FRAG_BUF->tag = uip_htons(my_tag); */
            SET16(RIME_FRAG_PTR, RIME_FRAG_TAG, my_tag);
            my_tag++;

            /* Copy payload and send */
            rime_hdr_len += SICSLOWPAN_FRAG1_HDR_LEN;

            //rime_payload_len = (MAC_MAX_PAYLOAD - framer_hdrlen - rime_hdr_len) & 0xf8;
            rime_payload_len = (MAC_MAX_PAYLOAD - rime_hdr_len) & 0xf8;

            PRINTFO("(len %d, tag %d)\n", rime_payload_len, my_tag);
            memcpy(rime_ptr + rime_hdr_len,
            (uint8_t *)UIP_IP_BUF + uncomp_hdr_len, rime_payload_len);
            packetbuf_set_datalen(rime_payload_len + rime_hdr_len);
            q = queuebuf_new_from_packetbuf();
            if(q == NULL)
            {
                uipPakcets_unlock();
                PRINTFO("could not allocate queuebuf for first fragment, dropping packet\n");
                return 0;
            }
            send_packet(&dest);
            queuebuf_to_packetbuf(q);
            queuebuf_free(q);
            q = NULL;

            /* Check tx result. */
            //        if((last_tx_status == MAC_TX_COLLISION) ||
            //           (last_tx_status == MAC_TX_ERR) ||
            //               (last_tx_status == MAC_TX_ERR_FATAL)) {
            //                   PRINTFO("error in fragment tx, dropping subsequent fragments.\n");
            //                   return 0;
            //               }
            /* set processed_ip_out_len to what we already sent from the IP payload*/
            processed_ip_out_len = rime_payload_len + uncomp_hdr_len;

            /*
             * Create following fragments
             * Datagram tag is already in the buffer, we need to set the
             * FRAGN dispatch and for each fragment, the offset
             */
            rime_hdr_len = SICSLOWPAN_FRAGN_HDR_LEN;
            /*     RIME_FRAG_BUF->dispatch_size = */
            /*       uip_htons((SICSLOWPAN_DISPATCH_FRAGN << 8) | uip_len); */
            SET16(RIME_FRAG_PTR, RIME_FRAG_DISPATCH_SIZE,
            ((SICSLOWPAN_DISPATCH_FRAGN << 8) | uip_len));

            //rime_payload_len = (MAC_MAX_PAYLOAD - framer_hdrlen - rime_hdr_len) & 0xf8;
            rime_payload_len = (MAC_MAX_PAYLOAD - rime_hdr_len) & 0xf8;

            while(processed_ip_out_len < uip_len)
            {
                PRINTFO("sicslowpan output: fragment ");
                RIME_FRAG_PTR[RIME_FRAG_OFFSET] = processed_ip_out_len >> 3;

                /* Copy payload and send */
                if(uip_len - processed_ip_out_len < rime_payload_len)
                {
                    /* last fragment */
                    rime_payload_len = uip_len - processed_ip_out_len;
                }

                PRINTFO("(offset %d, len %d, tag %d)\n",
                processed_ip_out_len >> 3, rime_payload_len, my_tag);
                memcpy(rime_ptr + rime_hdr_len,
                (uint8_t *)UIP_IP_BUF + processed_ip_out_len, rime_payload_len);
                packetbuf_set_datalen(rime_payload_len + rime_hdr_len);
                q = queuebuf_new_from_packetbuf();
                if(q == NULL)
                {
                    uipPakcets_unlock();
                    PRINTFO("could not allocate queuebuf, dropping fragment\n");
                    return 0;
                }
                send_packet(&dest);
                queuebuf_to_packetbuf(q);
                queuebuf_free(q);
                q = NULL;
                processed_ip_out_len += rime_payload_len;

                /* Check tx result. */
                //            if((last_tx_status == MAC_TX_COLLISION) ||
                //               (last_tx_status == MAC_TX_ERR) ||
                //                   (last_tx_status == MAC_TX_ERR_FATAL)) {
                //                       PRINTFO("error in fragment tx, dropping subsequent fragments.\n");
                //                       return 0;
                //                   }
            }
#else /* SICSLOWPAN_CONF_FRAG */
            PRINTFO("sicslowpan output: Packet too large to be sent without fragmentation support; dropping packet\n");
            return 0;
#endif /* SICSLOWPAN_CONF_FRAG */
        }
        else
        {
            /* Reentrant error . Just Drop the packet */
            if ( uip_len <= uncomp_hdr_len )
            {
                uipPakcets_unlock();
                UIP_STAT_SLP_TX_DROP_WRONG_LEN_INC;
                return 0;
            }

            /*
             * The packet does not need to be fragmented
             * copy "payload" and send
             */
            memcpy(rime_ptr + rime_hdr_len, (uint8_t *)UIP_IP_BUF + uncomp_hdr_len,
            uip_len - uncomp_hdr_len);
            packetbuf_set_datalen(uip_len - uncomp_hdr_len + rime_hdr_len);
            send_packet(&dest);
        }

        uipPakcets_unlock();
    }
    return 1;
}

/*--------------------------------------------------------------------*/
/** \brief Process a received 6lowpan packet.
 *  \param r The MAC layer
 *
 *  The 6lowpan packet is put in packetbuf by the MAC. If its a frag1 or
 *  a non-fragmented packet we first uncompress the IP header. The
 *  6lowpan payload and possibly the uncompressed IP header are then
 *  copied in siclowpan_buf. If the IP packet is complete it is copied
 *  to uip_buf and the IP layer is called.
 *
 * \note We do not check for overlapping sicslowpan fragments
 * (it is a SHALL in the RFC 4944 and should never happen)
 *
 * Out-of-Order packet support:
 *
 * Case 1: 6LP reassembly is ongoing, but we receive a different
 * fragmented packet (from any source address)
 *
 * Case 2: 6LP reassembly is ongoing, but we receive a
 * non-fragmented packet (from any source address)
 *
 */

extern uint8_t uip_in_progress;

static void input(void)
{
    /* size of the IP packet (read from fragment) */
    uint16_t frag_size = 0;
    /* offset of the fragment in the IP packet */
    uint8_t frag_offset = 0;
    //uint8_t is_fragment = 0;

    /* tag of the fragment */
    uint16_t frag_tag = 0;
    uint8_t first_fragment = 0, last_fragment = 0;

    /* init */
    uncomp_hdr_len = 0;
    rime_hdr_len = 0;

    /* The MAC puts the 15.4 payload inside the RIME data buffer */
    uipPacketIndex_inc(&uipPackets_ReadIdx);
    rime_ptr = uipPakcets[uipPackets_ReadIdx].buff + PACKETBUF_HDR_SIZE;

    //set working reass_index to 0
    reass_index = 0;
    uint8_t i;

    //check reassembly timers
    for (i = 1; i < REASS_BUF_SIZE; i++)
    {
        if (reass_structs[i].processed_ip_in_len != 0)
        {
            if (timer_expired(&(reass_structs[i].reass_timer)))
            {
                //if timer expired, drop packet
                reass_structs[i].sicslowpan_len = 0;
                reass_structs[i].processed_ip_in_len = 0;
                UIP_STAT_SLP_REASS_TIMEOUT_INC;
            }
        }
    }

    //reass_buf array full, if not set reass_first_empty_index
    uint8_t reass_first_empty_index = 0;
    for (i = 1; i < REASS_BUF_SIZE; i++)
    {
        if (reass_structs[i].processed_ip_in_len == 0)
        {
            reass_first_empty_index = i;
            break;
        }
    }

    /*
     * Check if current packet is first fragment, Nth fragment, or non-fragment
     * Since we don't support the mesh and broadcast header, the first header
     * we look for is the fragmentation header
     */
    switch ((GET16(RIME_FRAG_PTR, RIME_FRAG_DISPATCH_SIZE) & 0xf800) >> 8)
    {

    case SICSLOWPAN_DISPATCH_FRAG1:

        if (reass_first_empty_index == 0)
        {
            //reass_buf is full, drop current packet
            UIP_STAT_SLP_FULL_DROP_SEGMENT_INC;
            return;
        }
        UIP_STAT_SLP_FRAG_1_INC;

        PRINTFI("sicslowpan input: FRAG1 ");
        frag_offset = 0;
        /*       frag_size = (uip_ntohs(RIME_FRAG_BUF->dispatch_size) & 0x07ff); */
        frag_size = GET16(RIME_FRAG_PTR, RIME_FRAG_DISPATCH_SIZE) & 0x07ff;
        /*       frag_tag = uip_ntohs(RIME_FRAG_BUF->tag); */
        frag_tag = GET16(RIME_FRAG_PTR, RIME_FRAG_TAG);
        PRINTFI("size %d, tag %d, offset %d)\n",
                frag_size, frag_tag, frag_offset);
        rime_hdr_len += SICSLOWPAN_FRAG1_HDR_LEN;
        /*      printf("frag1 %d %d\n", reass_tag, frag_tag);*/
        first_fragment = 1;
        //is_fragment = 1;
        break;

    case SICSLOWPAN_DISPATCH_FRAGN:
        /*
         * set offset, tag, size
         * Offset is in units of 8 bytes
         */
        PRINTFI("sicslowpan input: FRAGN ");
        frag_offset = RIME_FRAG_PTR[RIME_FRAG_OFFSET];
        frag_tag = GET16(RIME_FRAG_PTR, RIME_FRAG_TAG);
        frag_size = GET16(RIME_FRAG_PTR, RIME_FRAG_DISPATCH_SIZE) & 0x07ff;
        PRINTFI("size %d, tag %d, offset %d)\n",
                frag_size, frag_tag, frag_offset);
        rime_hdr_len += SICSLOWPAN_FRAGN_HDR_LEN;

        //search through reass buf array to see if we're working on this packet
        //if yes, set reass_index
        //if not, return (drop)
        for (i = 1; i < REASS_BUF_SIZE; i++)
        {
            if ((reass_structs[i].reass_tag == frag_tag)
                    && (reass_structs[i].sicslowpan_len == frag_size)
                    && (rimeaddr_cmp(&(reass_structs[i].frag_sender),
                                     packetbuf_addr(PACKETBUF_ADDR_SENDER))))
            {
                reass_index = i;
                UIP_STAT_SLP_FRAG_N_INC;
                break;
            }
        }
        if (reass_index == 0)
        {
            //drop packet
            UIP_STAT_SLP_FRAG_N_HDR_ERROR_INC;
            return;
        }

        /* If this is the last fragment, we may shave off any extrenous
         bytes at the end. We must be liberal in what we accept. */
        if (reass_structs[i].processed_ip_in_len + packetbuf_datalen_read()
                - rime_hdr_len >= frag_size)
        {
            last_fragment = 1;
        }
        //is_fragment = 1;
        break;

    default:
        break;
    }

    if (first_fragment == 1)
    {
        reass_index = reass_first_empty_index;

        //setup reass buffer structure
        reass_structs[reass_index].sicslowpan_len = frag_size;
        reass_structs[reass_index].reass_tag = frag_tag;

        timer_set(&(reass_structs[reass_index].reass_timer),
                  SICSLOWPAN_REASS_MAXAGE * CLOCK_SECOND);
        rimeaddr_copy(&(reass_structs[reass_index].frag_sender),
                      packetbuf_addr(PACKETBUF_ADDR_SENDER));
    }

    if (rime_hdr_len != SICSLOWPAN_FRAGN_HDR_LEN)
    {
        /* this is not a FRAGN, do header compression dispatch */

        /* Process next dispatch and headers */
#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06
        if ((RIME_HC1_PTR[RIME_HC1_DISPATCH] & 0xe0) == SICSLOWPAN_DISPATCH_IPHC)
        {
            PRINTFI("sicslowpan input: IPHC\n");
            uncompress_hdr_hc06(frag_size);
        }
        else
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06 */
            switch (RIME_HC1_PTR[RIME_HC1_DISPATCH])
            {
#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC1
            case SICSLOWPAN_DISPATCH_HC1:
            PRINTFI("sicslowpan input: HC1\n");
            uncompress_hdr_hc1(frag_size);
            break;
#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC1 */
            case SICSLOWPAN_DISPATCH_IPV6:
                PRINTFI("sicslowpan input: IPV6\n");
                rime_hdr_len += SICSLOWPAN_IPV6_HDR_LEN;

                /* Put uncompressed IP header in sicslowpan_buf. */
                memcpy(SICSLOWPAN_IP_BUF, rime_ptr + rime_hdr_len, UIP_IPH_LEN);

                /* Update uncomp_hdr_len and rime_hdr_len. */
                rime_hdr_len += UIP_IPH_LEN;
                uncomp_hdr_len += UIP_IPH_LEN;
                break;
            default:
                /* unknown header, dropping packet */
                UIP_STAT_SLP_UNKNOWN_HDR_ERROR_INC;
                return;
            }
    }

    /*
     * copy "payload" from the rime buffer to the sicslowpan_buf
     * if this is a first fragment or not fragmented packet,
     * we have already copied the compressed headers, uncomp_hdr_len
     * and rime_hdr_len are non 0, frag_offset is.
     * If this is a subsequent fragment, this is the contrary.
     */
    if (packetbuf_datalen_read() < rime_hdr_len)
    {
        //packet dropped due to header > total packet
        UIP_STAT_SLP_PACKET_LEN_SMALL_INC;
        return;
    }
    rime_payload_len = packetbuf_datalen_read() - rime_hdr_len;

    /* Sanity-check size of incoming packet to avoid buffer overflow */
    {
        int req_size = UIP_LLH_LEN + uncomp_hdr_len
                + (uint16_t) (frag_offset << 3) + rime_payload_len;
        if (req_size > sizeof(sicslowpan_buf(0)))
        {
            //packet too big for uip_buf size, packet dropped
            UIP_STAT_SLP_PACKET_LEN_BIG_INC;
            return;
        }
    }

    memcpy((uint8_t *) SICSLOWPAN_IP_BUF + uncomp_hdr_len
                   + (uint16_t) (frag_offset << 3),
           rime_ptr + rime_hdr_len, rime_payload_len);

    /* update processed_ip_in_len if fragment, sicslowpan_len otherwise */

    if (frag_size > 0)
    {
        /* Add the size of the header only for the first fragment. */
        if (first_fragment != 0)
        {
            reass_structs[reass_index].processed_ip_in_len += uncomp_hdr_len;
        }
        /* For the last fragment, we are OK if there is extrenous bytes at
         the end of the packet. */
        if (last_fragment != 0)
        {
            reass_structs[reass_index].processed_ip_in_len = frag_size;
        }
        else
        {
            reass_structs[reass_index].processed_ip_in_len += rime_payload_len;
        }

    }
    else
    {
        reass_structs[reass_index].sicslowpan_len = rime_payload_len
                + uncomp_hdr_len;
    }

    /*
     * If we have a full IP packet in sicslowpan_buf, deliver it to
     * the IP stack
     */
    if (reass_structs[reass_index].processed_ip_in_len == 0
            || (reass_structs[reass_index].processed_ip_in_len
                    == reass_structs[reass_index].sicslowpan_len))
    {
        if (uip_input_len!= 0)
        {
            //FIFO full
            uipRxPacketLossCount++;
        }
        else
        {
            uipBuffer_LockWriteBuffer();
            memcpy((uint8_t *)UIP_IP_INPUT_BUF, (uint8_t *)SICSLOWPAN_IP_BUF, reass_structs[reass_index].sicslowpan_len);
            uip_input_len = reass_structs[reass_index].sicslowpan_len;

            UIP_STAT_SLP_RX_INC;

            //Need to set the uIPv6 variables for Link Layer address for sender and receiver before processing input
            rimeaddr_copy((rimeaddr_t*)&uip_dst_lladdr_input, packetbuf_addr(PACKETBUF_ADDR_RECEIVER));
            rimeaddr_copy((rimeaddr_t*)&uip_src_lladdr_input, packetbuf_addr(PACKETBUF_ADDR_SENDER));
            uipBufferIndex_inc(&uipBuffer_WriteIdx);
            uipBuffer_UnlockWriteBuffer();

            tcpip_process_handler(PACKET_INPUT, NULL);
        }
        reass_structs[reass_index].sicslowpan_len = 0;
        reass_structs[reass_index].processed_ip_in_len = 0;
    }
}
/** @} */

/*--------------------------------------------------------------------*/
/* \brief 6lowpan init function (called by the MAC layer)             */
/*--------------------------------------------------------------------*/
void sicslowpan_init(void)
{
    /*
     * Set out output function as the function to be called from uIP to
     * send a packet.
     */
    tcpip_set_outputfunc(output);

    //initalize reass_structs
    uint8_t i;
    for (i = 0; i < REASS_BUF_SIZE; i++)
    {
        reass_structs[i].sicslowpan_len = 0;
        reass_structs[i].processed_ip_in_len = 0;
        reass_structs[i].reass_tag = 0;
    }

#if SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06
    /* Preinitialize any address contexts for better header compression
     * (Saves up to 13 bytes per 6lowpan packet)
     * The platform contiki-conf.h file can override this using e.g.
     * #define SICSLOWPAN_CONF_ADDR_CONTEXT_0 {addr_contexts[0].prefix[0]=0xbb;addr_contexts[0].prefix[1]=0xbb;}
     */
#if SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS > 0
    addr_contexts[0].used = 1;
    addr_contexts[0].number = 0;
#ifdef SICSLOWPAN_CONF_ADDR_CONTEXT_0
    SICSLOWPAN_CONF_ADDR_CONTEXT_0;
#endif

#endif /* SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS > 0 */

#if SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS > 1
    {
        int i;
        for (i = 1; i < SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS; i++)
        {
#ifdef SICSLOWPAN_CONF_ADDR_CONTEXT_1
            if (i==1)
            {
                addr_contexts[1].used = 1;
                addr_contexts[1].number = 1;
                SICSLOWPAN_CONF_ADDR_CONTEXT_1;
#ifdef SICSLOWPAN_CONF_ADDR_CONTEXT_2
            }
            else if (i==2)
            {
                addr_contexts[2].used = 1;
                addr_contexts[2].number = 2;
                SICSLOWPAN_CONF_ADDR_CONTEXT_2;
#endif
            }
            else
            {
                addr_contexts[i].used = 0;
            }
#else
            addr_contexts[i].used = 0;
#endif /* SICSLOWPAN_CONF_ADDR_CONTEXT_1 */

        }
    }
#endif /* SICSLOWPAN_CONF_MAX_ADDR_CONTEXTS > 1 */

#endif /* SICSLOWPAN_COMPRESSION == SICSLOWPAN_COMPRESSION_HC06 */
}
/*--------------------------------------------------------------------*/
#if !ORIGINAL_CONTIKI
inline void sicslowpan_input(void)
{
    if (tcpip_isInitialized())
    {
        //Do not try to uncompress any packet if the uIPv6 Stack is not initialized!
        input();
    }
}

void sicslowpan_sent_cb(uint8_t status, uint8_t num_tx)
{
    packet_sent(NULL, status, num_tx);
}
#else //ORIGINAL_CONTIKI
const struct network_driver sicslowpan_driver =
{
    "sicslowpan",
    sicslowpan_init,
    input
};
#endif

void sicslowpan_setContext0(uint8_t *prefix)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        addr_contexts[0].prefix[i] = prefix[i];
    }
    addr_contexts[0].used = 1;
    addr_contexts[0].number = 0;
}
/*--------------------------------------------------------------------*/
/** @} */

#endif //WITH_SICSLOWPAN
