import java.io.OutputStream;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;

public class TcpClient {
    private static final DateTimeFormatter TS =
        DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");

    private static void log(String msg) {
        System.out.println("[B][" + LocalDateTime.now().format(TS) + "] " + msg);
        System.out.flush();
    }

    public static void main(String[] args) throws Exception {
        String host = System.getenv().getOrDefault("TARGET_HOST", "127.0.0.1");
        int port = Integer.parseInt(System.getenv().getOrDefault("TARGET_PORT", "5000"));
        long intervalMs = Long.parseLong(System.getenv().getOrDefault("SEND_INTERVAL_MS", "2000"));

        while (true) {
            try (Socket socket = new Socket(host, port)) {
                log("Connected to " + host + ":" + port);
                OutputStream out = socket.getOutputStream();

                int seq = 1;
                while (true) {
                    String payload = "hello from container-b seq=" + seq
                            + " time=" + LocalDateTime.now().format(TS) + "\n";
                    byte[] bytes = payload.getBytes(StandardCharsets.UTF_8);
                    out.write(bytes);
                    out.flush();

                    log("Sent " + bytes.length + " bytes -> " + payload.trim());
                    seq++;
                    Thread.sleep(intervalMs);
                }
            } catch (Exception e) {
                log("Connection failed / lost: " + e.getMessage());
                Thread.sleep(2000);
            }
        }
    }
}