# This is a central manager image for HTCondor
ARG EL
ARG VERSION
ARG SUFFIX

FROM htcondor/base:${VERSION}-el${EL}${SUFFIX}

ARG EL
ARG VERSION
ARG BUILDDATE

# ddavila: adding CA certs for GSI auth
RUN yum -y install https://repo.opensciencegrid.org/osg/3.5/osg-3.5-el7-release-latest.rpm
RUN yum -y install osg-ca-certs


# https://label-schema.org/rc1
LABEL org.label-schema.name="htcondor/cm:${VERSION}-el${EL}${SUFFIX}" \
      org.label-schema.description="HTCondor ${VERSION} central manager image for RHEL ${EL}-compatible" \
      org.label-schema.vendor="HTCondor" \
      org.label-schema.license="Apache-2.0"

COPY cm/condor/*.conf /etc/condor/config.d/

# ddavila: Adding a script used to push ganglia monitoring data to influxDB
# needs to be activated by adding the following in the configs:
# GANGLIA_GMETRIC = /usr/local/bin/metrics-influxdb-condor-gangliad
COPY cm/misc/metrics-influxdb-condor-gangliad /usr/local/bin/metrics-influxdb-condor-gangliad
RUN chmod a+x /usr/local/bin/metrics-influxdb-condor-gangliad


# We need ganglia for monitoring
RUN yum -y install ganglia ganglia-gmond ganglia-gmetad

EXPOSE 9618

LABEL org.label-schema.build-date="${BUILDDATE}"
