import type { IncomingMessage, ServerResponse } from "node:http";
import Metro from "metro";
import { argv } from "node:process";
import { harnessMetroConfig } from "./metro-config.ts";

const EPHEMERAL_PORT = 0;
const config = await Metro.loadConfig({ port: EPHEMERAL_PORT }, harnessMetroConfig);

const { httpServer } = await Metro.runServer(config, {
  host: "127.0.0.1",
  unstable_extraMiddleware: [
    (request: IncomingMessage, response: ServerResponse, next: () => void): void => {
      const [pathname] = (request.url ?? "").split("?");

      if (pathname !== "/status") {
        next();
        return;
      }

      response.setHeader("X-React-Native-Project-Root", config.projectRoot);
      response.end("packager-status:running");
    },
  ],
  // #81: Fast Refresh needs Metro to see edits; a golden run leaves the tree unwatched.
  watch: argv.includes("--watch"),
});
const address = httpServer.address();

if (address === null || typeof address === "string") {
  throw new TypeError("Metro is not listening on a TCP port");
}

process.stdout.write(`metro listening on ${address.port}\n`);
