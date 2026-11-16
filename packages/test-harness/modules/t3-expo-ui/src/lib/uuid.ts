let count = 0;

export const uuidv4 = () => `00000000-0000-4000-8000-${String(++count).padStart(12, "0")}`;
