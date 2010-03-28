#!/bin/bash  
#
# Script that makes a deployable archive. First argument is apended 
# to the archive name. 
#

ARCHIVE=credis${1}
FILES="credis.c credis.h credis-test.c Makefile README"

# remove archive directory if it exists
if [ -d ${ARCHIVE} ]; then
  rm -rf ${ARCHIVE}
fi

# remove deployable archive file if it exists
if [ -f ${ARCHIVE}.tar.gz ]; then
  rm -rf ${ARCHIVE}.tar.gz
fi

# create archive directory, add files and tar into a deployable archive
mkdir $ARCHIVE
cp ${FILES} ${ARCHIVE}
tar -czf ${ARCHIVE}.tar.gz ${ARCHIVE}
echo "done!"


