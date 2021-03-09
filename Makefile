all:
	cd hw && $(MAKE) build/gateware/top.txt
	cd sw/src && $(MAKE) all
	cd hw && $(MAKE) flash
