# AcreetBoot CVM relay container
# SPDX-License-Identifier: BSD-3-Clause
FROM python:3.12-alpine
WORKDIR /app
COPY relay.py /app/relay.py
EXPOSE 8477
ENV CVM_RECEIPTS=/data/receipts.jsonl
CMD ["python3", "/app/relay.py"]
