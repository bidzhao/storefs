package com.example.s3rdma;

import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.WebSocket;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * Minimal synchronous request/response wrapper around java.net.http.WebSocket,
 * sufficient for the small register/qpinfo/token JSON exchange used by the
 * RDMA control channel.
 */
class SyncWebSocket {

    private static final long TIMEOUT_SECONDS = 15;

    private final WebSocket ws;
    private final LinkedBlockingQueue<String> queue = new LinkedBlockingQueue<>();
    private final StringBuilder partial = new StringBuilder();

    SyncWebSocket(String url) throws Exception {
        HttpClient client = HttpClient.newHttpClient();
        CompletableFuture<WebSocket> cf = client.newWebSocketBuilder()
                .buildAsync(URI.create(url), new WebSocket.Listener() {
                    @Override
                    public CompletionStage<?> onText(WebSocket webSocket, CharSequence data, boolean last) {
                        partial.append(data);
                        if (last) {
                            queue.offer(partial.toString());
                            partial.setLength(0);
                        }
                        webSocket.request(1);
                        return null;
                    }

                    @Override
                    public void onError(WebSocket webSocket, Throwable error) {
                        queue.offer("\u0000ERROR\u0000" + error);
                    }

                    @Override
                    public CompletionStage<?> onClose(WebSocket webSocket, int statusCode, String reason) {
                        queue.offer("\u0000CLOSED\u0000");
                        return null;
                    }
                });
        this.ws = cf.get(TIMEOUT_SECONDS, TimeUnit.SECONDS);
    }

    void sendText(String text) {
        ws.sendText(text, true).join();
    }

    String recvText() throws Exception {
        String msg = queue.poll(TIMEOUT_SECONDS, TimeUnit.SECONDS);
        if (msg == null) {
            throw new RuntimeException("timeout waiting for websocket message");
        }
        if (msg.startsWith("\u0000ERROR\u0000")) {
            throw new RuntimeException("websocket error: " + msg.substring("\u0000ERROR\u0000".length()));
        }
        if (msg.equals("\u0000CLOSED\u0000")) {
            throw new RuntimeException("websocket closed unexpectedly");
        }
        return msg;
    }

    void close() {
        try {
            ws.sendClose(WebSocket.NORMAL_CLOSURE, "done").get(TIMEOUT_SECONDS, TimeUnit.SECONDS);
        } catch (Exception ignore) {
            ws.abort();
        }
    }
}
