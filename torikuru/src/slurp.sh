#!/bin/sh

for i in *_IDN_RDF
do
	exchange=`echo $i | sed -e 's/_IDN_RDF//'`
	echo "$i --> $exchange"
	../Torikuru \
		--session=ssled://nylabads2/IDN_RDF \
		--output-path=$exchange.refresh.dmp \
		--disable-update \
		--terminate-on-sync \
		--symbol-path=$i \
		2>&1 | tee $exchange.refresh.log
done
