FROM fedora:latest

WORKDIR /app

ADD sched_converter.tar.gz /app

RUN dnf -y install boost-devel cmake gcc gcc-c++ git && cd sched_converter/build && cmake -DBUILD_DEPS=ON .. && cmake --build .

EXPOSE 26900

CMD ["sched_converter/build/sched_converter", "0.0.0.0", "26900"]
