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
        rd5r-factory.conf \
        gd77-ver311-W5NOR-Oklahoma.conf \
        gd77-empty.conf \
        gd77-factory.conf \
        d868uv-rmham-2018-10-20.conf \
        d868uv-empty.conf

VPATH = ../../dmrconfig.wiki/files/uv380 \
        ../../dmrconfig.wiki/files/md380 \
        ../../dmrconfig.wiki/files/rd5r \
        ../../dmrconfig.wiki/files/gd77 \
        ../../dmrconfig.wiki/files/d868uv

all:    $(CONF)

clean:
	rm -f $(CONF)

.SUFFIXES: .conf .rdt .dat .img

.rdt.conf:
	dmrconfig $< > $@

.dat.conf:
	dmrconfig $< > $@

.img.conf:
	dmrconfig $< > $@
