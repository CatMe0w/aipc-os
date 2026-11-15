SUBDIRS = cfw aipc-boot opennboot gdbstub doom

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

aipc-boot: cfw opennboot

clean:
	for d in $(SUBDIRS); do $(MAKE) -C $$d clean || exit 1; done

.PHONY: all clean $(SUBDIRS)
