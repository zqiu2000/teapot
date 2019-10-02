/*
 * $Tea$
 * SPDX-License-Identifier: GPL-2.0
 */

#define DEBUG
#define pr_fmt(fmt) "teapot: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/firmware.h>
#include <linux/mm.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/delay.h>

#include <asm/apic.h>

#define TEA_ENTRY	(0x1000)
#define TEA_NAME	"tea.bin"
#define TEA_SIZE		(32*1024) /*32K*/

static const struct firmware *fw_tea;
static unsigned int teapot;

static uint32_t get_phy_apid(unsigned int cpu)
{
	return apic->cpu_present_to_apicid(cpu);
}

static int cleanup_teapot(void)
{
	unsigned int cpu;
	int ret;

	/* Offline and reserved the cpu for tea to run, current solution is to offline the last one cpu */
	if (num_online_cpus() == 1) {
		pr_warn("Uni-processor system?\n");
		/* Allow the case that last cpu had been offlined and waiting. */
		/*return -EINVAL;*/
	}

	if (!cpumask_equal(cpu_possible_mask, cpu_online_mask)) {
		cpumask_var_t possible_cpu;

		pr_warn("Some cpu already plug out? Please check system cpu configuration!\n");

		ret = zalloc_cpumask_var(&possible_cpu, GFP_KERNEL);
		if (!ret)
			return -ENOMEM;

		cpumask_copy(possible_cpu, cpu_possible_mask);

		cpu = cpumask_last(possible_cpu);
		cpumask_clear_cpu(cpu, possible_cpu);

		if (cpumask_equal(possible_cpu, cpu_online_mask)) {
			pr_warn("The last cpu%d already offline? Will load tea on that cpu!\n", cpu);
			free_cpumask_var(possible_cpu);
			goto __bail_out;
		} else {
			free_cpumask_var(possible_cpu);
			return -EINVAL;
		}
	}

	cpu = cpumask_last(cpu_online_mask);

	if (!cpu_is_hotpluggable(cpu)) {
		pr_err("You have to enable cpu hotplug in system!\n");
		return -EINVAL;
	}

	pr_info("Will offline cpu%d\n", cpu);

	ret = cpu_down(cpu);
	if (ret) {
		pr_err("Take down cpu%d failed, error %d\n", cpu, ret);
		return ret;
	}

__bail_out:

	teapot = cpu;

	return 0;
}

static int fill_boiled_water(uint32_t phys_apicid, uint32_t tea_entry)
{
	unsigned long send_status, accept_status;
	int init_udelay = 10000;

	pr_debug("Asserting INIT\n");

	/*
	 * Turn INIT on target chip
	 */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT,
			   phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	udelay(init_udelay);

	pr_debug("Deasserting INIT\n");

	/* Target chip */
	/* Send IPI */
	apic_icr_write(APIC_INT_LEVELTRIG | APIC_DM_INIT, phys_apicid);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	/*
	 * STARTUP IPI
	 */
	apic_icr_write(APIC_DM_STARTUP | (tea_entry >> 12),
			   phys_apicid);

	/*
	 * Give the other CPU some time to accept the IPI.
	 */
	if (init_udelay == 0)
		udelay(10);
	else
		udelay(300);

	pr_debug("Waiting for send to finish...\n");
	send_status = safe_apic_wait_icr_idle();

	/*
	 * Give the other CPU some time to accept the IPI.
	 */
	if (init_udelay == 0)
		udelay(10);
	else
		udelay(200);

	/* STARTUP IPI again */
	apic_icr_write(APIC_DM_STARTUP | (tea_entry >> 12),
			   phys_apicid);

	accept_status = (apic_read(APIC_ESR) & 0xEF);
	pr_info("Kick done, send status %lx, accept status %lx\n", send_status, accept_status);

	return 0;
}

static int load_tea(void)
{
	int ret;
	void *tea_va;

	tea_va = ioremap_nocache(TEA_ENTRY, TEA_SIZE);

	memset(tea_va, 0, TEA_SIZE);

	ret = request_firmware(&fw_tea, TEA_NAME, NULL);
	if (ret) {
		pr_err("No %s found\n", TEA_NAME);
		return -ENODEV;
	}

	/* Load the tea */
	memcpy(tea_va, fw_tea->data, fw_tea->size);

	iounmap(tea_va);

	return ret;
}

static int __init teapot_init(void)
{
	int ret;

	pr_info("teapot init\n");

	ret = cleanup_teapot();
	if (ret)
		return -ENODEV;

	ret = load_tea();
	if (ret)
		return -ENODEV;

	/* Fill boiled water and enjoy */
	return fill_boiled_water(get_phy_apid(teapot), TEA_ENTRY);
}

static void __exit teapot_exit(void)
{
	pr_info("teapot exit!\n");
	release_firmware(fw_tea);
}

module_init(teapot_init);
module_exit(teapot_exit);

MODULE_DESCRIPTION("Teapot driver");

MODULE_AUTHOR("Qiu,Zanxiong <zqiu2000@126.com>");
MODULE_LICENSE("GPL v2");
