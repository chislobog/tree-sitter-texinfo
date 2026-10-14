#!/usr/bin/env perl
# Extracts the Texinfo inputs of texi2any's own parser tests
# (tta/perl/t/*.t in the Texinfo sources) into one .texi file per test case,
# as OUTPUT/SUITE/CASE.texi.
#
# Each test file builds a list of cases (name, input, options) and passes it
# to run_all() from t/test_utils.pl. This evaluates the test files with a
# run_all() that writes the inputs out instead of running them, so Perl
# itself interprets the string literals.
#
# Usage: extract-texi2any-tests.pl TESTS_DIR OUTPUT_DIR

use strict;
use warnings;

use Cwd qw(abs_path getcwd);
use File::Basename qw(basename);
use File::Copy qw(copy);
use File::Path qw(make_path);
use File::Temp qw(tempdir);

die "usage: $0 TESTS_DIR OUTPUT_DIR\n" unless @ARGV == 2;
my $tests = abs_path($ARGV[0]);
make_path($ARGV[1]);
my $output = abs_path($ARGV[1]);

# Stand-ins for what the test files load, in a directory they find via
# `use lib '.'` and `require 't/test_utils.pl'`.
my $stubs = tempdir(CLEANUP => 1);
make_path("$stubs/Texinfo", "$stubs/t");
write_file("$stubs/Texinfo/ModulePath.pm",
           "package Texinfo::ModulePath;\nsub import {}\n1;\n");
write_file("$stubs/t/test_utils.pl", "1;\n");

# The HTML and EPUB tests ask whether the C converter is enabled.
sub Texinfo::XSLoader::XS_convert_enabled { return 0; }

my $suite;
my $count = 0;
# Cases whose input is a file this doesn't have: the test files' own
# input_files/ is extracted, not the other test suites' files they name.
my @missing;

sub write_file {
  my ($path, $content) = @_;
  open(my $fh, '>', $path) or die "$path: $!\n";
  binmode($fh, utf8::is_utf8($content) ? ':encoding(UTF-8)' : ':raw');
  print $fh $content;
  close($fh);
}

# Called by each test file with its list of cases.
sub run_all {
  my (undef, $cases) = @_;
  make_path("$output/$suite");
  foreach my $case (@$cases) {
    my ($case_name, $input, $options) = @$case;
    my $path = "$output/$suite/$case_name.texi";
    if ($options and defined($options->{'test_file'})) {
      my $file = "$tests/input_files/$options->{'test_file'}";
      unless (-f $file) {
        push @missing, "$suite/$case_name";
        next;
      }
      copy($file, $path) or die "$file: $!\n";
    } elsif (defined($input)) {
      write_file($path, $input);
    } else {
      next;
    }
    $count++;
  }
}

my $cwd = getcwd();
chdir($stubs) or die "$stubs: $!\n";
foreach my $file (sort glob("$tests/*.t")) {
  $suite = basename($file, '.t');
  do $file;
  # A test file that doesn't load would drop its cases unnoticed.
  die "$file: $@" if $@;
}
chdir($cwd);

die "no test inputs found in $tests\n" unless $count;
print "$count test inputs written to $output\n";
printf "%d cases left out, their input being another suite's file: %s\n",
  scalar(@missing), join(', ', @missing) if @missing;
