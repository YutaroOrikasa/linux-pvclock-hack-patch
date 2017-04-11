
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <asm/pvclock.h>
#include <asm/msr.h>

#include <linux/clocksource.h>
#include  <linux/list.h>
#include <linux/stacktrace.h>

struct list_head *get_clocksource_list(void);

struct stack_trace *get_kvm_clock_read_trace(int *);
void reset_traces(void);


size_t snprint_clock_data(char *buf, size_t n, struct clocksource *cs)
{

	cycle_t csnow;
	int64_t cs_nsec;
	int cs_total_sec;
	/* size_t total = 0; */
	/* size_t tmp; */

	csnow = cs->read(cs);
	cs_nsec = clocksource_cyc2ns(csnow, cs->mult, cs->shift);
	cs_total_sec = cs_nsec / 1000 / 1000 / 1000;
	return snprintf(buf, n,
			"%s cs_now: %llx cs->mult %x , cs->shift %x total_nsec %lld total_sec: %d\n",
			cs->name, csnow, cs->mult, cs->shift, cs_nsec, cs_total_sec);
}

size_t snprint_all_clock_data(char *buf, size_t n, struct list_head *clocksource_list)
{

	struct clocksource *cs;
	size_t total = 0;
	list_for_each_entry(cs, clocksource_list, list) {
		size_t tmp;
		tmp = snprint_clock_data(buf, n, cs);
		buf += tmp;
		total += tmp;
		n -= tmp;
	}
	return total;
}


static size_t snprint_pvclock_info(char *buf, size_t n)
{
	struct pvclock_vsyscall_time_info *hv_clock = pvclock_pvti_cpu0_va();
	struct pvclock_vcpu_time_info *src;

	if (!hv_clock) {
		return snprintf(buf, n, "no hv_clock\n");
	} else {
		size_t size = 0;
		size_t size_tmp;
		//printk(
		src = &hv_clock[0].pvti;
		size_tmp = snprintf(buf, n,
				    "hv_clock(ptr): %pK pvti .version: %u .system_time: %llu .tsc_to_system_mul: %u .tsc_shift: %d .flags: %x .tsc_timestamp: %llu rdtsc(): %llu\n",
				    hv_clock,
				    src->version,
				    src->system_time,
				    src->tsc_to_system_mul,
				    (s32)src->tsc_shift,
				    (u32)src->flags,
				    src->tsc_timestamp,
				    rdtsc_ordered()
				   );
		size += size_tmp;
		buf += size_tmp;
		n -= size_tmp;

		size_tmp = snprintf(buf, n, ".system_time sec: %d\n",
				    ((int)(src->system_time / 1000 / 1000 / 1000)));
		size += size_tmp;
		buf += size_tmp;
		n -= size_tmp;

		size_tmp = snprintf(buf, n, ".tsc_timestamp: %llx\n", src->tsc_timestamp);
		size += size_tmp;
		buf += size_tmp;
		n -= size_tmp;

		size_tmp = snprintf(buf, n, "rdtsc():        %llx\n", rdtsc_ordered());
		size += size_tmp;
		buf += size_tmp;
		n -= size_tmp;

		return size;
	}
}

#define DEVICE_NAME "clockdebug"

static ssize_t clock_debug_sprint_all(char *buf)
{
	size_t total = 0;
	size_t tmp;
	size_t n = PAGE_SIZE;

	tmp = snprint_pvclock_info(buf, n);
	total += tmp;
	buf += tmp;
	n -= tmp;

	tmp = snprint_all_clock_data(buf, n, get_clocksource_list());
	total += tmp;
	buf += tmp;
	n -= tmp;
	return total;
}


static ssize_t clock_debug_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return clock_debug_sprint_all(buf);
}

static ssize_t clock_debug_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf_,
				 size_t count)
{
	char buf[1024];
	clock_debug_sprint_all(buf);
	pr_emerg("%s", buf);
	return count;
}

static ssize_t logging_tick_do_update_jiffies64_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	buf[0] = '?';
	return 1;
}

void set_logging_tick_do_update_jiffies64(char);

static ssize_t logging_tick_do_update_jiffies64_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	if (count == 0) {
		return 0;
	}

	if (buf[0] == '0') {
		printk("stop logging_tick_do_update_jiffies64\n");
		set_logging_tick_do_update_jiffies64(0);
	} else {
		set_logging_tick_do_update_jiffies64(1);
		printk("start logging_tick_do_update_jiffies64\n");
	}
	return count;
}

extern cycle_t extra_cycle;

static ssize_t extra_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n", extra_cycle);
}

static ssize_t extra_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf,
			   size_t count)
{
	u64 extra;
	sscanf(buf, "%llu", &extra);
	extra = ((extra * 1000 * 1000 * 1000) << 0x17) / 0x800000;
	pr_emerg("add extra cycle %llx\n", extra);
	extra_cycle += extra;
	return count;
}


static ssize_t trace_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf,
			   size_t count)
{

	int i, max;
	struct stack_trace *traces = get_kvm_clock_read_trace(&max);

	if (count != 0 && buf[0] == '0') {
		pr_emerg("reset trace\n");
		reset_traces();
		return count;
	}

	for (i = 0; i < max; i++) {
		printk("------- trace of kvm_clock_read ---------\n");
		print_stack_trace(&traces[i], 4);
	}
	reset_traces();
	return count;
}


static DEVICE_ATTR(debug, 0644, clock_debug_show, clock_debug_store);
static DEVICE_ATTR(logging_tick_do_update_jiffies64, 0644,
		   logging_tick_do_update_jiffies64_show, logging_tick_do_update_jiffies64_store);
static DEVICE_ATTR(extra, 0644, extra_show, extra_store);
static DEVICE_ATTR(trace, 0644, 0, trace_store);

static struct attribute *my_attrs[] = {
	&dev_attr_debug.attr,
	&dev_attr_logging_tick_do_update_jiffies64.attr,
	&dev_attr_extra.attr,
	&dev_attr_trace.attr,
	NULL,   /* NULL terminating is required */
};

static struct attribute_group my_attr_group = {
	.attrs = my_attrs,
};

static struct kobject *my_kobj;

/*
 * Called when module is load
 */
int init_module(void)
{
	int retval;

	/* /sys/kernel/$DIRを作成 */
	my_kobj = kobject_create_and_add(KBUILD_MODNAME,
					 kernel_kobj);


	if (!my_kobj) {
		return -ENOMEM;
	}

	retval = sysfs_create_group(my_kobj, &my_attr_group);

	if (retval) {
		kobject_put(my_kobj);
	}

	pr_warn("module loaded\n");
	return retval;
}

void cleanup_module(void)
{
	kobject_put(my_kobj);
	pr_warn("module cleanup\n");
}

MODULE_LICENSE("GPL");

