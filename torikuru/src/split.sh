#!/bin/sh

export PERL5LIB="`pwd`/.."

for i in *_IDN_RDF
do
	exchange=`echo $i | sed -e 's/_IDN_RDF//'`
	echo "$i --> $exchange"
	(
		mkdir $exchange 2> /dev/null
		cd $exchange
		../../asset.pl ../$exchange.refresh.csv
	)
done
