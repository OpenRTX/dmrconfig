CONF  = md2017-7-23-2017.conf \
        md2017-DualHotspot-PNW-2017-07-19.conf \
        md380-baynet-full-codeplug-rev1.conf \
        md380-generic-norcal-uhf-20161103b.conf \
        md380-norcal-brandmeister.conf \
        md-uv380_bm_2018-08-07.conf \
        md-uv380-empty.conf \
        md-uv380-factory.conf

VPATH = ../../dmrconfig.wiki/files

all:    $(CONF)

clean:
	rm -f $(CONF)

.SUFFIXES: .conf .rdt

.rdt.conf:
	dmrconfig $< > $@
