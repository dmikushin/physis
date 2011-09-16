#!/usr/bin/env bash
#
# Detects options and path settings and runs configure and make.
# 
# Usage:
# rose-build.sh <src-path> <install-path>:
# - src-path: unpacked ROSE source directory
# - install-path: installation path (should be an absolute path)
#
# After options are determined, each of the following commands can be selected.
# - configure
# - make
# - make install

#set -u
set -e

while getopts ":r:p:b:" opt; do
	case $opt in
		r)
			ROSE_SRC=$OPTARG
			;;
		p)
			INSTALL_PREFIX=$OPTARG
			;;
		b)
			BOOST=$OPTARG
			;;
		\?)
			echo "Invalid option: -$OPTARG" >&2
			;;
	esac
done

shift $(($OPTIND - 1))

if [ -z "$ROSE_SRC" ]; then
	echo -n "ROSE source path: "
	read ROSE_SRC
fi
if [ -z "$INSTALL_PREFIX" ]; then
	echo -n "Install prefix path: "
	read INSTALL_PREFIX
fi

echo Building ROSE at $ROSE_SRC and installing it to $INSTALL_PREFIX
CONFIG_OPTIONS=$*

BOOST_CANDIDATES=($HOME/homebrew /usr $HOME/tools/boost /work0/GSIC/apps/boost/1_45_0/gcc)

if [ -z "$BOOST" ]; then
	for c in ${BOOST_CANDIDATES[*]}; do
		if [ -d $c/include/boost ]; then
			BOOST=$c
			break
		fi
	done
fi

echo Using Boost found at $BOOST

if [ -z "$JAVA_HOME" ]; then
	case $OSTYPE in
		linux*)
			LDFLAGS=""
			for i in $(locate libjvm.so); do
				if echo $i | grep --silent -e gcc -e gcj -e openjdk; then continue; fi
				echo -n "Using $i? [Y/n] "
				read yn
				if [ "$yn" != "n" ]; then
					export JAVA_HOME=${i%/jre*}
					break
				fi
			done
			
			if [ -z "$JAVA_HOME" ]; then
				echo "Error: no Java found"
				exit 1
			fi
			;;
		darwin*)
			JAVA_HOME=/System/Library/Frameworks/JavaVM.framework/Versions/CurrentJDK
			;;
	esac
fi

JVM_DIR=$(dirname $(find ${JAVA_HOME}/ -name "libjvm\.*" | head -1))
case $OSTYPE in
	linux*)
		JAVA_LIBRARIES=${JAVA_HOME}/jre/lib:$JVM_DIR
		;;
	darwin*)
		JAVA_LIBRARIES=$JVM_DIR
		;;
esac

# Is this necessary?
#export LDFLAGS="-Wl,-rpath=${JAVA_LIBRARIES}:${BOOST}/lib"
case $OSTYPE in
	linux*)
		export LD_LIBRARY_PATH=${JAVA_LIBRARIES}:${BOOST}/lib:$LD_LIBRARY_PATH
		;;
	darwin*)
		export DYLD_LIBRARY_PATH=${JAVA_LIBRARIES}:${BOOST}/lib:$DYLD_LIBRARY_PATH
		;;
esac

echo LDFLAGS is set to $LDFLAGS
echo LD_LIBRARY_PATH is set to $LD_LIBRARY_PATH
echo JAVA_HOME is set to $JAVA_HOME
echo set LD_LIBRARY_PATH to $LD_LIBRARY_PATH

echo "Detecting number of cores..."
case $OSTYPE in
	linux*)
		NUM_PROCESSORS=$(grep processor /proc/cpuinfo|wc -l)
		;;
	darwin*)
		NUM_PROCESSORS=$(system_profiler |grep 'Total Number Of Cores' | awk '{print $5}')
		;;
esac

echo "$NUM_PROCESSORS cores detected"

function exec_configure()
{
	echo $ROSE_SRC/configure --prefix=$INSTALL_PREFIX --with-CXX_DEBUG=-g --with-CXX_WARNINGS="-Wall -Wno-deprecated" --with-boost=$BOOST --enable-languages=c,c++,fortran,cuda,opencl --disable-binary-analysis-tests -with-haskell=no $CONFIG_OPTIONS		
	echo -n "Type Enter to proceed: "
	read x
	$ROSE_SRC/configure --prefix=$INSTALL_PREFIX --with-CXX_DEBUG=-g --with-CXX_WARNINGS="-Wall -Wno-deprecated" --with-boost=$BOOST --enable-languages=c,c++,fortran,cuda,opencl --enable-binary-analysis-tests=no --disable-projects-directory --disable-tutorial-directory -with-haskell=no $CONFIG_OPTIONS
	if [ $? == 0 ]; then
		echo "Rerun again and select make for building ROSE"
	fi
}

function exec_make()
{
	echo building ROSE by make -j$((NUM_PROCESSORS / 2))
	make -j$((NUM_PROCESSORS / 2))
	if [ $? == 0 ]; then
		echo "Rerun again and select make install to install ROSE"
	fi
}

function exec_install()
{
	echo installing ROSE
	make install
}

echo "Commands"
echo "1: configure"
echo "2: make"
echo "3: make install"
echo "4: configure && make && make install"
echo -n "What to do? [1-4] (default: 4): "
read command
if [ "x$command" = "x" ]; then command="4"; fi
case $command in
	1)
		exec_configure
		;;
	2)
		exec_make
		;;
	3)
		exec_install
		;;
	4)
		exec_configure
		exec_make
		exec_install
		;;
	*)
		echo Invalid input \"$command\"
		;;
esac
