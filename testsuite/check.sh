#! /bin/sh
# check script for GNU ed - The GNU line editor
# Copyright (C) 2006-2025 Antonio Diaz Diaz.
#
# This script is free software; you have unlimited permission
# to copy, distribute, and modify it.

LC_ALL=C
export LC_ALL
objdir=`pwd`
testdir=`cd "$1" ; pwd`
ED="${objdir}"/ed
framework_failure() { echo "failure in testing framework" ; exit 1 ; }

if [ ! -f "${ED}" ] || [ ! -x "${ED}" ] ; then
	echo "${ED}: cannot execute"
	exit 1
fi

if [ -d tmp ] ; then rm -rf tmp ; fi
mkdir tmp
cd "${objdir}"/tmp || framework_failure

cp "${testdir}"/test.txt test.txt || framework_failure
cp "${testdir}"/test.bin test.bin || framework_failure
touch empty || framework_failure			# used also by r1.ed
fail=0
test_failed() { fail=1 ; printf " $1" ; [ -z "$2" ] || printf "($2)" ; }

printf "testing ed-%s...\n" "$2"

"${ED}" -q nx_file < empty
[ $? = 2 ] || test_failed $LINENO
"${ED}" -q +0 test.txt < empty				# invalid line number
[ $? = 1 ] || test_failed $LINENO
"${ED}" -qs +/foobar test.txt < empty			# no match
[ $? = 1 ] || test_failed $LINENO
"${ED}" -qs +?foobar test.txt < empty			# no match
[ $? = 1 ] || test_failed $LINENO
"${ED}" -qs +/[a-z test.txt < empty			# syntax error
[ $? = 1 ] || test_failed $LINENO
"${ED}" -qs -l +/foobar test.txt < empty		# -l has no effect
[ $? = 1 ] || test_failed $LINENO
echo "q" | "${ED}" -qs +/foobar test.txt || test_failed $LINENO
echo "p" | "${ED}" -s +7 test.txt | grep -q 'animated' || test_failed $LINENO
echo "p" | "${ED}" -s +7 test.txt | grep -q 'the' && test_failed $LINENO
echo "p" | "${ED}" -s +/which test.txt | grep -q 'must' || test_failed $LINENO
echo "p" | "${ED}" -s +?which test.txt | grep -q 'ease' || test_failed $LINENO
echo "p" | "${ED}" -s +/[A-Z] test.txt | grep -q 'two' || test_failed $LINENO
echo "p" | "${ED}" -s +?[A-Z] test.txt | grep -q 'even' || test_failed $LINENO
# test that a second 'e' succeeds
printf "a\nHello world!\n.\ne test.txt\nf foo.txt\nf\nh\nH\nH\nkx\nl\nn\np\nP\nP\ny\n.z\n# comment\n=\n!:\n.\ne test.txt\n8p\n" | "${ED}" -s | grep -q 'agrarian' || test_failed $LINENO
echo "q" | "${ED}" -q 'name_with_bell.txt' && test_failed $LINENO
echo "q" | "${ED}" -q --unsafe-names 'name_with_bell.txt' || test_failed $LINENO

if [ ${fail} != 0 ] ; then echo ; fi

# Run the .err scripts first with a regular file connected to standard
# input, since these don't generate output; they exit with nonzero status.
for i in "${testdir}"/*.err ; do
	if "${ED}" -s test.txt < "$i" > /dev/null 2>&1 ; then
		echo "*** The script $i exited abnormally ***"
		fail=127
	fi
done

# Run the .err scripts again with a regular file connected to standard
# input, but with '--loose-exit-status'; they should exit with zero status.
for i in "${testdir}"/*.err ; do
	if "${ED}" -sl test.txt < "$i" > /dev/null 2>&1 ; then
		true
	else
		echo "*** The script $i failed '--loose-exit-status' ***"
		fail=127
	fi
done

# Run the .err scripts again as pipes - these should exit with nonzero
# status without altering the contents of the buffer; the produced
# 'out.ro' must be identical to 'test.txt'.
for i in "${testdir}"/*.err ; do
	base=`echo "$i" | sed 's,^.*/,,;s,\.err$,,'`	# remove dir and ext
	if cat "$i" | "${ED}" -s test.txt > /dev/null 2>&1 ; then
		echo "*** The piped script $i exited abnormally ***"
		fail=127
	else
		if cmp -s out.ro test.txt ; then
			true
		else
			mv -f out.ro ${base}.ro
			echo "*** Output ${base}.ro of piped script $i is incorrect ***"
			fail=127
		fi
	fi
	rm -f out.ro
done

# Run the .ed scripts and compare their output against the .r files, which
# contain the correct output.
# The .ed scripts should exit with zero status.
for i in "${testdir}"/*.ed ; do
	base=`echo "$i" | sed 's,^.*/,,;s,\.ed$,,'`	# remove dir and ext
	if "${ED}" test.txt < "$i" > out.log 2>&1 ; then
		if cmp -s out.o "${testdir}"/${base}.r ; then
			true
		else
			mv -f out.o ${base}.o
			echo "*** Output ${base}.o of script $i is incorrect ***"
			fail=127
		fi
	else
		mv -f out.log ${base}.log
		echo "*** The script $i exited abnormally ***"
		fail=127
	fi
	rm -f out.o out.log
done

rm -f test.txt test.bin empty

if [ ${fail} = 0 ] ; then
	echo "tests completed successfully."
	cd "${objdir}" && rm -r tmp
else
	echo "tests failed."
	echo "Please, send a bug report to bug-ed@gnu.org"
	echo "Include the (compressed) contents of '${objdir}/tmp' in the report."
fi
exit ${fail}
