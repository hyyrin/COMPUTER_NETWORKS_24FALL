FROM ubuntu:22.04

RUN apt-get update && \
    DEBAIN_FRONTEND=noninteractive apt-get install -qy build-essential zlib1g-dev libssl-dev tmux

# for log_checker
RUN apt install -y python3 pip
RUN pip install numpy colorama more_itertools

CMD ["bash"]