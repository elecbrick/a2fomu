all:
	cd sw/src && $(MAKE) all
	cd hw && $(MAKE) flash
