// Transient animation hints shared between the store/drag code and the views.

/** pileId -> world position the pile should visually "settle" from on its next render. */
export const origins = new Map<string, { x: number; y: number; lifted?: boolean }>();
