import Metro from "metro";
import { harnessMetroConfig } from "./metro-config.ts";

const EPHEMERAL_PORT = 0;

const { httpServer } = await Metro.runServer(await Metro.loadConfig({ port: EPHEMERAL_PORT }, harnessMetroConfig), {
  host: "127.0.0.1",
  watch: false,
});
const address = httpServer.address();

if (address === null || typeof address === "string") {
  throw new TypeError("Metro is not listening on a TCP port");
}

process.stdout.write(`metro listening on ${address.port}\n`);
