/** The element with `id`, which the page's own HTML always has. */
export function getElement(id: string): HTMLElement {
  const element = document.getElementById(id);
  if (!element) {
    throw new Error(`The page has no #${id}`);
  }
  return element;
}
