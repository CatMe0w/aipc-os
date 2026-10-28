SUBDIRS = cfw aipc-boot opennboot gdbstub doom

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

# aipc-boot carries the CFW image, so cfw builds first.
aipc-boot: cfw

clean:
	for d in $(SUBDIRS); do $(MAKE) -C $$d clean || exit 1; done

.PHONY: all clean $(SUBDIRS)
