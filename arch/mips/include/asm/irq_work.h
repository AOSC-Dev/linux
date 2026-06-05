static inline bool arch_irq_work_has_interrupt(void)
{
	return IS_ENABLED(CONFIG_MACH_LOONGSON64) && IS_ENABLED(CONFIG_SMP);
}
