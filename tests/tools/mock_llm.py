#!/usr/bin/env python3
"""mock_llm.py - stand-in for an OpenAI-compatible LLM (Ollama, LM Studio ...)
for end-to-end tests of the LLM backend without a model.

    python3 mock_llm.py [port]        (default 11500)

POST /v1/chat/completions answers like a well-behaved model would:
  * translation requests: every element of the JSON array becomes
    "<LANG>: <text>" (German-looking text is returned unchanged, to exercise
    the "already in your language" path); <k>...</k> segments are kept.
  * romanization requests: a few known words, otherwise "romanized(...)".
Every request is appended to mock_llm.log (one JSON object per line).
If a file "mock_drop" exists in the working directory, batches come back one
element short (like a small local model that merges lines).
"""
import json
import os
import re
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

GERMAN = {"hallo", "ich", "wir", "danke", "und", "nicht", "ist", "der", "die", "das", "gruppe"}
ROMAN = {"مرحبا": "marhaba", "你好": "ni hao", "привет": "privet", "شكرا": "shukran"}
LANG_CODES = {"German": "DE", "English": "EN", "French": "FR", "Arabic": "AR", "Spanish": "ES",
              "Chinese": "ZH", "Russian": "RU", "Japanese": "JA"}


def translate(text, target):
    words = set(re.findall(r"\w+", text.lower()))
    if target == "DE" and words & GERMAN:
        return text
    if target == "AR":  # real Arabic script, to exercise the script warning and Ctrl+U
        return f"مرحبا {text}"
    # A "visible" translation; <k>...</k> segments stay exactly as they are.
    return f"{target}: {text}"


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length).decode("utf-8"))
        with open("mock_llm.log", "a", encoding="utf-8") as log:
            log.write(json.dumps(body, ensure_ascii=False) + "\n")
        system = body["messages"][0]["content"]
        items = json.loads(body["messages"][1]["content"])
        if "Latin letters" in system:
            answer = [ROMAN.get(items[0].strip(), f"romanized({items[0]})")]
        else:
            m = re.search(r"into (.+?)\.\n", system)
            name = m.group(1) if m else "English"
            target = next((code for key, code in LANG_CODES.items() if key in name), name[:2].upper())
            answer = [translate(t, target) for t in items]
            if os.path.exists("mock_drop") and len(answer) > 1:
                answer = answer[:-1]  # misbehave like a small model: one line goes missing
        reply = {"choices": [{"message": {"role": "assistant",
                                          "content": "```json\n" + json.dumps(answer, ensure_ascii=False) + "\n```"}}]}
        data = json.dumps(reply, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 11500
    HTTPServer(("127.0.0.1", port), Handler).serve_forever()
