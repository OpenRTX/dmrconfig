CONF  = md2017-7-23-2017.conf \
        md2017-DualHotspot-PNW-2017-07-19.conf \
        md380-baynet-full-codeplug-rev1.conf \
        md380-generic-norcal-uhf-20161103b.conf \
        md380-norcal-brandmeister.conf \
        md-uv380_bm_2018-08-07.conf \
        md-uv380-empty.conf \
        md-uv380-factory.conf \
        rd5r-baden-wuertemburg-codeplug-v3.conf \
        rd5r-bayern-codeplug-v3.conf \
        rd5r-s-yorks-codeplug-v3.conf \
        rd5r-v21x-Charlotte-NC.conf \
        rd5r-v21x-Ft-Launderdale-FL.conf \
        rd5r-v21x-Los-Angeles-CA.conf \
        rd5r-v21x-Tampa-FL.conf \
        rd5r-empty.conf \
        rd5r-factory.conf

VPATH = ../../dmrconfig.wiki/files

all:    $(CONF)

clean:
	rm -f $(CONF)

.SUFFIXES: .conf .rdt .dat

.rdt.conf:
	dmrconfig $< > $@

.dat.conf:
	dmrconfig $< > $@
