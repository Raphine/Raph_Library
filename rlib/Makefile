OBJS = net.o task.o spinlock.o libglobal.o thread.o mem/uvirtmem.o tty.o queue.o

DEPS= $(filter %.d, $(subst .o,.d, $(OBJS)))

.PHONY: clean net.o

default: ../rlib.o

-include $(DEPS)

../rlib.o: $(OBJS)
	ld -r $^ -o $@

net.o:
	make -C net CFLAGS="$(CFLAGS)" CXXFLAGS="$(CXXFLAGS)" ASFLAGS="$(ASFLAGS)"

clean:
	-rm -f $(OBJS) $(TEST_OBJS) $(DEPS) *.s *.ii
