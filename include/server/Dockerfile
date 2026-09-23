FROM denoland/deno AS builder
WORKDIR /app

RUN apt update && apt install unzip

COPY . .
RUN deno task prepare

FROM debian:stable-slim
WORKDIR /app

COPY --from=builder /app/server .

ENTRYPOINT ["/app/server"]
