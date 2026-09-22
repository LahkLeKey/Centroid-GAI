import "dotenv/config";
import { defineConfig as ormConfig } from "@prisma/orm-postgres/config";
import { definePrismaConfig } from "prisma/config";

const connection = process.env["DATABASE_URL"];

if (!connection) {
  throw new Error("DATABASE_URL is required");
}

export default definePrismaConfig({
  orm: ormConfig({
    contract: "./src/prisma/contract.prisma",
    db: { connection },
    migrations: { dir: "./migrations" },
  }),
  skills: { check: false },
});

