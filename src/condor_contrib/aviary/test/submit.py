#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2009-2011 Red Hat, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# uses Suds - https://fedorahosted.org/suds/
from suds import *
from suds.client import Client
from sys import exit, argv
import time, pwd, os
import logging
import argparse
from aviary.https import *

# NOTE: Suds has had little support for adding attributes
# to the request body until 0.4.1
# uncomment the following to enable the allowOverrides attribute

#from suds.plugin import MessagePlugin
#from suds.sax.attribute import Attribute
#class OverridesPlugin(MessagePlugin):
    #def marshalled(self, context):
        #sj_body = context.envelope.getChild('Body')[0]
        #sj_body.attributes.append(Attribute("allowOverrides", "true"))

uid = pwd.getpwuid(os.getuid())[0]
if not uid:
    uid = "condor"

# change these for other default locations and ports
wsdl = 'file:/var/lib/condor/aviary/services/job/aviary-job.wsdl'
url = 'http://localhost:9090/services/job/submitJob'
key = '/etc/pki/tls/certs/client.key'
cert = '/etc/pki/tls/certs/client.crt'
client = Client(wsdl);

parser = argparse.ArgumentParser(description='Submit a job remotely via SOAP.')
parser.add_argument('-v','--verbose', action="store_true",default=False, help='enable SOAP logging')
parser.add_argument('-u','--url', action="store", nargs='?', dest='url', help='http or https URL prefix to be added to cmd')
parser.add_argument('-k','--key', action="store", nargs='?', dest='key', help='client SSL key file')
parser.add_argument('-c','--cert', action="store", nargs='?', dest='cert', help='client SSL certificate file')
args =  parser.parse_args()

if args.url and "https://" in args.url:
	url = args.url
	client = Client(wsdl,transport = HTTPSClientCertTransport(key,cert))

# NOTE: the following form to enable attribute additions
# is only supported with suds >= 0.4.1
#client = Client(job_wsdl,plugins=[OverridesPlugin()]);
client.set_options(location=url)

# enable to see service schema
if args.verbose:
	logging.basicConfig(level=logging.INFO)
	logging.getLogger('suds.client').setLevel(logging.DEBUG)
	print client

# add specific requirements here
req1 = client.factory.create("ns0:ResourceConstraint")
req1.type = 'OS'
req1.value = 'LINUX'
reqs = [ req1 ]

# add extra Condor-specific or custom job attributes here
extra1 = client.factory.create("ns0:Attribute")
extra1.name = 'RECIPE'
extra1.type = 'STRING'
extra1.value = '"SECRET_SAUCE"'
extras = [ extra1 ]

try:
	result = client.service.submitJob( \
	# the executable command
		'/bin/sleep', \
	# some arguments for the command
		'120', \
	# the submitter name
		uid, \
	# initial working directory wwhere job will execute
		'/tmp', \
	# an arbitrary string identifying the target submission group
		'python_test_submit', \
	# special resource requirements
		reqs,	\
	# additional attributes
		extras
	)
except Exception, e:
	print "invocation failed at: ", url
	print e
	exit(1)	

if result.status.code != "OK":
	print result.status.code,"; ", result.status.text
	exit(1)

if args.verbose:
	print result
else:
	print result.id.job;
