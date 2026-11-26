FROM python:3.13-slim
WORKDIR /app
COPY receiver.py .
RUN mkdir /data && chown 65532:65532 /data
USER 65532:65532
ENV REPORT_BIND=0.0.0.0 REPORT_DATA=/data
EXPOSE 8080
CMD ["python", "-u", "receiver.py"]
