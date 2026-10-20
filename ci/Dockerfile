# Reference Linux environment (PRD: Linux/WSL2). Dependencies exactly as in the README.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake git ca-certificates curl \
      libosmium2-dev libprotozero-dev zlib1g-dev libbz2-dev libexpat1-dev \
      nodejs \
    && rm -rf /var/lib/apt/lists/*
RUN git config --global --add safe.directory '*'
WORKDIR /work
# Usage: docker run --rm -v "$PWD":/src:ro dispatchlab-ci \
#          bash -c 'git clone -q /src /work/repo && cd /work/repo && ./scripts/verify.sh'
