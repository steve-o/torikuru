#!/usr/bin/perl
# -*- perl -*-

use File::Basename;
use File::Path qw(mkpath);
use Text::CSV_PP;

my @recordtypemap = qw(
    0    0    0    0    0    0    0    0    0    0    0    0    0    0    0    0
    0 BOND FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 BOND    0    0    0
    0 BOND FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 BOND    0    0    0  
    0 BOND FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 BOND    0    0    0
    0 BOND FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 BOND    0    0    0
    0 BOND FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 BOND    0    0    0
 EQTY EQTY FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 FUTR    0    0    0
 EQTY EQTY FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 FUTR    0    0    0
    0 EQTY FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 FUTR    0    0    0
    0 EQTY FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 FUTR    0    0    0
    0 EQTY FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 FUTR    0    0    0
    0 EQTY FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 FUTR    0    0    0
    0 EQTY FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 FUTR    0    0    0
    0 FORX FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 FUTR    0    0    0
    0    0    0    0    0    0    0    0    0    0    0    0    0    0    0    0
    0 MNYM FUTR FUTR FUTR    0 INDX    0 LINK    0    0    0 FUTR    0    0    0
);

if (!@ARGV) {
	die "usage: $0 <CSV>\n";
}

my $parser = Text::CSV_PP->new();

foreach my $file (@ARGV)
{
	my $filename = basename ($file);
	open MOO, "<$file" or die "cannot open: $!";
	$_ = <MOO>;
	chomp;
	@fids = split(/,/);
	$i = 0;
	foreach (@fids) {
		$symbol_idx = $i if (/symbol/);
		$recordtype_idx = $i if (/RECORDTYPE/);
		$i++;
	}
	while(<MOO>) {
		my $recordtype, $path;
		$parser->parse ($_);
		@msg = $parser->fields();
		if ($recordtype_idx > $#msg) {
			$recordtype = 0;
		} else {
			$recordtype = $msg[$recordtype_idx];
		}
		$path = "asset_partitioned/$recordtypemap[$recordtype].$recordtype";
		mkpath ($path);
		open BAA, ">>$path/$filename" or die "cannot open: $!";
		print BAA "$msg[$symbol_idx]\n";
		close BAA;
	}
	close MOO;
}
