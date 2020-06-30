/*
 * $Tea$
 * SPDX-License-Identifier: GPL-2.0
 */

#define DEBUG
#define pr_fmt(fmt) "ipc-tea: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <asm/irq.h>
#include <asm/irq_vectors.h>
#include <asm/ipi.h>
#include <asm/nmi.h>

#include "config.h"
#include "msg_defs.h"

static mesg_channel *mesg_tx;
static mesg_channel *mesg_rx;

static inline void send_ipi_single(int cpu, int vector)
{
	apic->send_IPI(cpu, vector);
}

int send_msg_async(void)
{
	struct mesg_payload *mesg = &mesg_tx->mesg[mesg_tx->tx_id];
	char buff[MSG_BYTES];
	static int cnt = 0;

	sprintf(buff, "efgh%d", cnt++);

	mesg->mesg_type = MESG_TYPE_ASYNC;
	strncpy(mesg->mesg_data.raw_dat, buff, strlen(buff));

	mesg_tx->tx_id++;
	mesg_tx->tx_id %= MSG_CNT;
	if (mesg_tx->tx_id == mesg_tx->rx_id) {
		pr_err("ipc: tx channel overflow!!\n");
	}

	/* Ring the bell */
	send_ipi_single(num_possible_cpus() - 1, X86_PLATFORM_IPI_VECTOR);

	pr_debug("send back msg %s\n", buff);

	return 0;
}

int recv_msg_async(void *buf)
{
	struct mesg_payload *mesg = &mesg_rx->mesg[mesg_rx->rx_id];

	/* mesg->mesg_type = MESG_TYPE_ASYNC; */
	memcpy(buf, mesg->mesg_data.raw_dat, MSG_BYTES);

	mesg_rx->rx_id++;
	mesg_rx->rx_id %= MSG_CNT;

	return 0;
}

int ipc_irq_callback(unsigned int type, struct pt_regs *regs)
{
	char buff[MSG_BYTES];

	pr_debug("ipc callback, type %d, rxid %d, txid %d\n", type, mesg_rx->rx_id, mesg_rx->tx_id);

	if (type != NMI_LOCAL || mesg_rx->rx_id == mesg_rx->tx_id)
		return NMI_DONE;

	while (mesg_rx->rx_id != mesg_rx->tx_id) {
		int cnt = 0;
		recv_msg_async((void*)buff);
		pr_debug("ipc: received %d msg %s\n", ++cnt, buff);
		send_msg_async();
	}

	return NMI_HANDLED;
}

static int __init ipc_tea_init(void)
{
	void *shmem;

	pr_info("Tea ipc init!\n");

	shmem = ioremap(SHARE_MEM_ADDR, SHARE_MEM_SIZE);

	memset(shmem, 0, SHARE_MEM_SIZE);

	mesg_rx = (mesg_channel *)shmem;
	mesg_tx = mesg_rx + 1;

	register_nmi_handler(NMI_LOCAL, ipc_irq_callback, 0, "ipc_tea");

	return 0;
}

static void __exit ipc_tea_exit(void)
{
	void *shmem = (void *)mesg_rx;

	pr_info("Tea ipc exit!\n");

	unregister_nmi_handler(NMI_LOCAL, "ipc_tea");
	iounmap(shmem);
}

module_init(ipc_tea_init);
module_exit(ipc_tea_exit);

MODULE_DESCRIPTION("Tea ipc driver");
MODULE_AUTHOR("Qiu,Zanxiong <zqiu2000@126.com>");
MODULE_LICENSE("GPL v2");
