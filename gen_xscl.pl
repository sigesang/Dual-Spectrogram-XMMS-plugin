#!/usr/bin/perl

print "int gen_xscl[48]={ ";
for($i=0;$i<48;$i++) {
    print "\n        " if( $i !=0 && $i%16 == 0);
    print int(exp(($i+1)/48*log(256))-1);
    print ", " if($i!=47);
    
}
print "};\n";
