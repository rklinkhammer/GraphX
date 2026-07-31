import { createRoot } from "react-dom/client";
import "@xyflow/react/dist/style.css";

import App from "./App";
import "./styles.css";

const root = document.getElementById("root");
if (!root) {
  throw new Error("GraphX dashboard root element is missing");
}

createRoot(root).render(<App />);
