FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gdb \
    clang \
    clang-format \
    cmake \
    ninja-build \
    git \
    strace \
    pkg-config \
    ca-certificates \
    sudo \
    && rm -rf /var/lib/apt/lists/* \
    && update-ca-certificates

# Создаем пользователя, чтобы файлы не были root-owned
ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=1000
RUN groupadd --gid ${USER_GID} ${USERNAME} \
    && useradd -s /bin/bash --uid ${USER_UID} --gid ${USER_GID} -m ${USERNAME} \
    && echo "${USERNAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USERNAME}

WORKDIR /work
USER ${USERNAME}

CMD ["bash"]