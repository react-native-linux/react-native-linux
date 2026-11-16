export function alert(title: string, message?: string) {
  console.log(`Dialog.alert ${title}: ${message ?? ""}`);
}
