import { fileURLToPath } from 'node:url';

export const rulesFile = fileURLToPath(new URL('../规则.md', import.meta.url));
export const matchSeed = (_map, seed) => seed ?? 11;
