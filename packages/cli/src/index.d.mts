export interface CliOptions { cwd?: string; stdout?: (text: string) => void; stderr?: (text: string) => void }
export declare function runCli(argv: string[], options?: CliOptions): Promise<number>;
export declare function atomicBundle(cwd: string, entry: string, outfile: string, root?: string): Promise<unknown>;
export declare function bundleApp(cwd: string, entry: string, outfile: string, root?: string, write?: boolean): Promise<unknown>;
