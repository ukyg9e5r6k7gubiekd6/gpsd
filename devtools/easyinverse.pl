#! /bin/perl
#
# From: Reuben Settergren <rjs@jhu.edu>
#
# I wrote a perl script to randomly generate matrices with smallish integers,
# and test for a suitable determinant (typically +/-1), and ouput the matrix
# and its inverse.
#
# There are switches to make it -s[ymmetric] and/or -d[iagonally strong],
# which are necessary and good properties for covariance matrices. You may
# need to grab Math::MatrixReal and Math::Factor::XS from cpan.

use Math::MatrixReal;
use Math::Factor::XS qw(prime_factors);
use Getopt::Std;
$opt{n} = 1;
getopts('hsdn:', \%opt);

$usage .= "easyinverse.pl [-hsd] [-n N]\n";
$usage .= "  -h     this message\n";
$usage .= "  -s     symmetric\n";
$usage .= "  -d     diagonally dominant\n";
$usage .= "  -n N   number to generate (DEF 1)\n";
if ($opt{h}) {
  print $usage;
  exit;
}

sub randint{ # but not 0
  my $min = shift or -5;
  my $max = shift or +5;

  my $ansa = 0;
  while ($ansa == 0) {
    my $r = rand;
    $r *= ($max - $min + 1);
    my $i = int($r);
    $ansa = $min + $i;
  }
  return $ansa;
}

$size = 4;

sub printmateq {
  my $lbl = shift;
  my $mat = shift;
  my $round = shift;

  if ($round) {
    for my $r (1..$size) {
    for my $c (1..$size) {
      my $elt = $mat->element($r,$c);
      my $pelt = abs($elt);
      my $prnd = int($pelt * $round + 0.5) / $round;
      my $rnd = $prnd * ($elt < 0 ? -1 : 1);
      $mat->assign($r, $c, $rnd);
    }}
  }

  my @rows;
  for my $r (1..$size) {
    my @row = map {$mat->element($r, $_)} (1..$size);
    my $rowstr = '{' . (join ',', @row) . '}';
    push @rows, $rowstr;
  }
  my $matstr = '{' . (join ',', @rows) . '}';
  $matstr =~ s!\.?000+\d+!!g;
  print "$lbl = $matstr;\n";
}



for (1..$opt{n}) { # requested number of matrices

  $M = new Math::MatrixReal($size,$size);

  while (1) {
    for $r (1..$size) {
      if ($opt{d}) { $M->assign($r,$r, randint(5,10)) }
      else         { $M->assign($r,$r, randint(-3, 3)) }

      for $c ($r+1..$size) {
	$i = randint(-5,5);
	$M->assign($r, $c, $i);
	$M->assign($c, $r, ($opt{s} ? $i : randint(-5,5)));
      }
    }

    $det = int($M->det + 0.5);
    next if $det == 0;
    @factors = prime_factors(abs($det));
    $all_good_factors = 1; # cross your fingers!
    for $f (@factors) {
      if ($f != 2 || $f != 5) {
	$all_good_factors = 0;
	last;
      }
    }
    next unless $all_good_factors;

    # test for positive semidefinite ~ all positive eigenvalues
    $evals = $M->sym_eigenvalues;
    $all_positive_evals = 1; # keep hope alive!
    for $r (1..$size) {
      if ($evals->element($r,1) <= 0) {
	$all_positive_evals = 0;
	last;
      }
    }
    last if $all_positive_evals;
  }

  printmateq(".mat", $M);
  printmateq(".inv", $M->inverse, 1000);

}


