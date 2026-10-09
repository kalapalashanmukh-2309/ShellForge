FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends build-essential && rm -rf /var/lib/apt/lists/*
WORKDIR /build
COPY Makefile ./
COPY include/ include/
COPY src/ src/
COPY examples/ examples/
RUN make -j2

FROM python:3.12-slim-bookworm
RUN useradd --create-home --uid 10001 shellforge
WORKDIR /app
COPY --from=build /build/bin/ bin/
COPY web/ web/
RUN mkdir -p web/workspace && chown -R shellforge:shellforge web/workspace
USER shellforge
EXPOSE 8765
CMD ["python3", "web/server.py", "--host", "0.0.0.0"]
