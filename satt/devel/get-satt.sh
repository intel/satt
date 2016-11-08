#!/bin/bash

# Copyright (c) 2015 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

export SAT_HOME=$PWD
export SATT_GIT_URL_PREFIX="https://"
export SATT_GIT_URL="github.com"
# export SATT_GIT_PORT="12345"
export SATT_GIT_PROJECT="01org/satt"
# export https_proxy=http://proxy.example.com:888

function help_and_exit {
    echo "get-satt.sh "
    echo "              -l --latest,  get latest SATT release"
    echo "              -v --version, get given SATT version e.g. -v 1.2.0"
    echo "              -h --help, print this info"
    exit
}

function get_latest_version {
    SAT_VERSION=$(git describe --tags `git rev-list --tags --max-count=1`)
}

#
# Handle command line arguments
#
while [[ $# > 0 ]]
do
key="$1"

case $key in
    -l|--latest)
    SAT_VERSION="LATEST"
    ;;
    -v|--version)
    SAT_VERSION="$2"
    shift # past argument
    ;;
    -h|--help)
    help_and_exit
    ;;
    *)
    echo "Unknown argument"
    help_and_exit
            # unknown option
    ;;
esac
shift # past argument or value
done

# Configure git for easier workflow
function configure_gerrit {
    echo "Configure git for gerrit workflow"
    URL=$SATT_GIT_URL_PREFIX$SATT_GIT_URL:$SATT_GIT_PORT/$SATT_GIT_PROJECT
    if [ -z "$SATT_GIT_PORT" ]; then
        URL=$SATT_GIT_URL_PREFIX$SATT_GIT_URL/$SATT_GIT_PROJECT
    fi
    echo "[remote \"for-release\"]
    url = $URL
    push = HEAD:refs/for/master" >> .git/config

    if [ -z "$SATT_GIT_PORT" ]; then
        scp -p $SATT_GIT_URL:hooks/commit-msg .git/hooks/
    else
        scp -p -P $SATT_GIT_PORT $SATT_GIT_URL:hooks/commit-msg .git/hooks/
    fi

    # Limit change id generation to only master branch
    sed -i s/add_ChangeId$// .git/hooks/commit-msg

    echo 'BRANCH=`git symbolic-ref --short HEAD`
if [ "$BRANCH" = "master" ]; then
    add_ChangeId
fi' >> .git/hooks/commit-msg

}

function check_capstone {
if [ ! -d "$SAT_HOME/src/parser/capstone-master" ] ; then
	deploy_capstone
fi
}

function deploy_capstone {
	pushd .
	mkdir -p src
	cd src
	mkdir -p parser
	cd parser
	wget https://github.com/aquynh/capstone/archive/master.tar.gz
	tar xvf master.tar.gz
	rm master.tar.gz
	cd capstone-master
	mkdir binaries
	change_capstone_prefix
	CAPSTONE_ARCHS="x86" CAPSTONE_STATIC=yes ./make.sh
	./make.sh install
	popd
}

function change_capstone_prefix {
	sed -i "/PREFIX ?= \/usr/c\PREFIX ?= $SAT_HOME/src/parser/capstone-master/binaries" Makefile
}

function check_udis86 {
if [ ! -d "$SAT_HOME/src/parser/udis86-master/binaries" ] ; then
	deploy_udis86
fi
}

function deploy_udis86 {
	pushd .
    mkdir -p src
    cd src
    makdir -p parser
	cd parser
	wget https://github.com/vmt/udis86/archive/master.tar.gz
	tar xvf master.tar.gz
	rm master.tar.gz
	cd udis86-master
	mkdir binaries
	./autogen.sh
	./configure --enable-static --prefix=$SAT_HOME/src/parser/udis86-master/binaries
	make
	make install
	popd
}

# Check git
function check_git {
	ret=$?
	if ! test "$ret" -eq 0
	then
		echo >&2 ""
		echo >&2 "**************************************************************"
		echo >&2 "*** SAT Deployment Failed"
		echo >&2 "*** "
	    echo >&2 "*** git clone failed with exit status $ret"
	    echo >&2 "*** Perhaps you do not have access rights to secure Git?"
	    echo >&2 "**************************************************************"
	    exit 1
	fi
}

# Update git repo
# - stash all current changes
# - fetch latest changes
# - reset to origin / master
function update_git {
    # Stash changes
    GIT_NAME="$(git config --get user.name)"
    GIT_EMAIL="$(git config --get user.email)"
    if [ -z "$GIT_NAME" ]; then
        git config --add user.name "deploy-sat automatic"
    fi
    if [ -z "$GIT_EMAIL" ]; then
        git config --add user.email "deploy-sat@my-dev-pc"
    fi
    git stash --include-untracked
    git fetch origin
    if [ -n "$SAT_VERSION" ]; then
        if [ "$SAT_VERSION" == "LATEST" ]; then
            get_latest_version
        fi
        git reset --hard "$SAT_VERSION"
    else
        git reset --hard origin/master
    fi
}

# Check git
function check_scons {
	ret=$?
	if ! test "$ret" -eq 0
	then
		echo >&2 ""
		echo >&2 "**************************************************************"
		echo >&2 "*** SAT Deployment Failed"
		echo >&2 "*** "
	    echo >&2 "*** scons building failed with exit status $ret"
	    echo >&2 "**************************************************************"
	    exit 1
	fi
}

# Check for dependencies
check_deps () {
DEPS=$(echo {scons,libelf-dev,python-pip,git,build-essential})
for i in $DEPS ; do
    dpkg-query -W -f='${Package}\n' | grep ^$i$ > /dev/null
    if [ $? != 0 ] ; then
        echo ""
		echo "**************************************************************"
		echo "*** SAT Deployment Failed"
		echo "*** "
        echo "*** These packages are needed for building SAT-tool: "
		echo "*** apt-get install scons libdisasm-dev libelf-dev python-pip git build-essential"
		echo "**************************************************************"
		exit 1
    fi
done
}

# Check if .git folders exists
detect_updagrade () {
PATHS=$(echo {".git",})
count=0
fcount=0
for i in $PATHS ; do
    if [ ! -d "$i" ] ; then
		let "fcount += 1"
	fi
	let "count += 1"
done

if [ "$fcount" == "0" ] ; then
	upgrade=1
	return
fi
#ah bad way... fix some day
if [ "$fcount" == "1" ] ; then
	upgrade=0
	return
fi

echo ""
echo "**************************************************************"
echo "*** SAT Install / Upgrade Failed"
echo "*** "
echo "*** Try clearing directory totally and run deploy-sat.sh again"
echo "**************************************************************"
upgrade=2
exit
}

#
# WE START FROM HERE
#
check_deps

upgrade=0
detect_updagrade

# Install
if [ "$upgrade" == "0" ]; then

echo "**************************************************************"
echo "***"
echo "*** Do you want to install SAT-tool in to current folder?"
echo "***"
echo "*** Installation folder = $PWD"
echo "***"
echo "*** If not Exit using CTRL + C or continue <ANY KEY>"
echo "***"
echo "**************************************************************"

read -s -n 1 dummy_input

echo "**************************************************************"
echo "***         Check out SAT-tool first time"
echo "**************************************************************"

# Remove deploy-sat.sh only from current directory
if [ -f "deploy-sat.sh" ]; then
    rm deploy-sat.sh
fi
if [ -f "get-satt.sh" ]; then
    rm get-satt.sh
fi

URL=$SATT_GIT_URL_PREFIX$SATT_GIT_URL:$SATT_GIT_PORT/$SATT_GIT_PROJECT
if [ -z "$SATT_GIT_PORT" ]; then
    URL=$SATT_GIT_URL_PREFIX$SATT_GIT_URL/$SATT_GIT_PROJECT
fi
git clone $URL .
check_git

if [ -n "$SAT_VERSION" ]; then
    if [ "$SAT_VERSION" == "LATEST" ]; then
        get_latest_version
    fi
    git checkout "$SAT_VERSION"
fi

configure_gerrit

#check_udis86
check_capstone

# Install satt into path
echo Create satt link to /usr/bin folder, requires sudo
if ! [ "$( readlink /usr/bin/satt )" = "$SAT_HOME/bin/satt" ]; then
    sudo rm -rf /usr/bin/satt
    sudo ln -s $SAT_HOME/bin/satt /usr/bin/satt
fi

fi # if [ "$upgrade" == "0" ]; then

# Upgrade to latest
if [ "$upgrade" == "1" ]; then
echo "**************************************************************"
echo "***         Update SAT-tool to latest"
echo "**************************************************************"

#check_udis86
check_capstone

update_git

if ! grep -Fxq "[remote \"for-release\"]" .git/config; then
  configure_gerrit
fi

fi

echo "**************************************************************"
echo "**** Building SAT-parser"
echo "**************************************************************"
pushd .
cd src/parser/post-processor
#scons flags="-static -I $SAT_HOME/src/parser/udis86-master/binaries/include -L $SAT_HOME/src/parser/udis86-master/binaries/lib"
scons flags="-static -I $SAT_HOME/src/parser/capstone-master/binaries/include -L $SAT_HOME/src/parser/capstone-master/binaries/lib -L $SAT_HOME/src/parser/capstone-master/binaries/lib64"
check_scons
popd

pushd .
cd satt/process
tar xzf $SAT_HOME/src/parser/post-processor/sat-post-processor-binaries.tgz
popd

if [ "$upgrade" == "0" ]; then
echo ""
echo "**************************************************************"
echo "***"
echo "*** To use SAT you need to source SAT environment by:"
echo "***"
echo "*** #> satt config"
echo "***"
echo "***"
echo "**************************************************************"
echo "*** Quick Quide"
echo "***"
echo "*** Tracing happens using #> satt trace"
echo "***"
echo "*** Processing traces     #> satt process <folder-where-trace-was-stored>"
echo "***"
echo "*** Importing & GUI       #> satt visualize -p <folder-where-trace-was-stored>"
echo "***"
echo "*** Launching GUI only    #> satt visualize"
echo "***"
echo "**************************************************************"
echo "***"
echo "*** If you are using your own builds, manual steps to build SAT-kernel module are needed"
echo "***"
echo "*** Setup environment         #> satt config"
echo "*** Build sat kernel module   #> satt build"
echo "***"
echo "**************************************************************"
else
echo "**************************************************************"
echo "***"
if [ -n "$SAT_VERSION" ]; then
    echo "*** SAT-tool updated to $SAT_VERSION release"
else
    echo "*** SAT-tool updated to latest release"
fi
echo "***"
echo "**************************************************************"
echo "***"
echo "*** If you are using your own builds, manual steps to build SAT-kernel module are needed"
echo "***"
echo "*** Setup environment         #> satt config"
echo "*** Build sat kernel module   #> satt build"
echo "***"
echo "**************************************************************"
fi
