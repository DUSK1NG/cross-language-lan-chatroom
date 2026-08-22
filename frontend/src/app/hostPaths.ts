function directoryAndSeparator(path: string): { directory: string; separator: string } {
  const separator = path.includes('\\') ? '\\' : '/';
  const index = Math.max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
  return {
    directory: index >= 0 ? path.slice(0, index) : '',
    separator
  };
}

export function inferPrivateKeyPath(serverExe: string, certFile: string): string {
  const certificate = certFile.trim();
  if (certificate) {
    const { directory, separator } = directoryAndSeparator(certificate);
    const fileName = certificate.slice(Math.max(certificate.lastIndexOf('/'), certificate.lastIndexOf('\\')) + 1);
    const stem = fileName.replace(/\.[^.]+$/, '');
    if (stem) return directory ? `${directory}${separator}${stem}.key` : `${stem}.key`;
  }

  const server = serverExe.trim();
  if (server) {
    const { directory, separator } = directoryAndSeparator(server);
    if (directory) return `${directory}${separator}certs${separator}server-lan.key`;
  }
  return '';
}
