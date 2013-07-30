#!/bin/sh

for i in *_IDN_RDF
do
	exchange=`echo $i | sed -e 's/_IDN_RDF//'`
	echo "$i --> $exchange"
	../Torikuru \
		--session=ssled://nylabads2/IDN_RDF \
		--input-path=$exchange.refresh.dmp \
		--output-path=$exchange.refresh.csv
done
