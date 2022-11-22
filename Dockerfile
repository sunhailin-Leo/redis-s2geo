FROM redis:6.0.16 AS build-env

RUN apt update && apt install -y gcc g++ cmake libgtest-dev openssl libssl-dev git

WORKDIR /data
RUN git clone https://github.com/abseil/abseil-cpp.git && cd abseil-cpp && mkdir build && cd build && \
    cmake -DCMAKE_INSTALL_PREFIX="/usr/local" -DCMAKE_CXX_STANDARD=14 -DCMAKE_POSITION_INDEPENDENT_CODE=ON .. && \
    make -j $(nproc) && make install
WORKDIR /data
RUN git clone https://github.com/google/s2geometry.git && cd s2geometry && mkdir build && cd build && \
    cmake -DGTEST_ROOT=/usr/src/gtest -DCMAKE_PREFIX_PATH=/usr/local -DCMAKE_CXX_STANDARD=14 .. && \
    make -j $(nproc) && make test ARGS="-j$(nproc)" && make install
WORKDIR /data
RUN git clone https://github.com/sunhailin-Leo/redis-s2geo.git && cd redis-s2geo && mkdir build && cd build && \
    cmake -DS2_PATH="/usr/local" -DABSL_PATH="/usr/local" -DCMAKE_CXX_STANDARD=14 .. && \
    make -j $(nproc) && cp libredis-s2geo.so.0.0.2 /opt
RUN rm -rf abseil-cpp s2geometry redis-s2geo && apt-get clean

FROM redis:6.0.16-alpine
COPY --from=build-env /usr/local/include/absl /usr/local/include/absl
COPY --from=build-env /usr/local/include/s2 /usr/local/include/s2
COPY --from=build-env /usr/local/lib /usr/local/lib
COPY --from=build-env /opt /opt
RUN apk add --no-cache libstdc++6 libstdc++ libc6-compat && ln -s /lib/libc.musl-x86_64.so.1 /lib/ld-linux-x86-64.so.2