// ? Lixeira Inteligente - Dashboard...
let ip = "";
const MAX_POINTS = 30;
const statusEl = document.getElementById("status-connection");

// ? Elemetnso Cards
const FC04decibeisE1 = document.getElementById("FC04decibeis");

// ? Gráficos lists
const labels = [];
const FC04decibeisData = [];

function getGradient(ctx, color_1, color_2) {
  const gradient = ctx.createLinearGradient(0, 0, 0, 400);
  gradient.addColorStop(0, color_1);
  gradient.addColorStop(1, color_2);
  return gradient;
}

const shadowLinePlugin = {
  id: "shadowLine",
  beforeDatasetsDraw(chart) {
    const ctx = chart.ctx;
    chart.data.datasets.forEach((dataset, i) => {
      const meta = chart.getDatasetMeta(i);
      if (!meta.hidden && dataset.borderColor) {
        ctx.save();
        ctx.shadowColor = dataset.boderColor + "88";
        ctx.shadowBlur = 12;
        ctx.shadowOffsetX = 0;
        ctx.shadowOffsetY = 4;
        ctx.globalAlpha = 0.7;
      }
    });
  },
  afterDatasetDraw(chart) {
    chart.ctx.restore();
  },
};

// ? Grafico de Gás
const ctxDecibeis = document.getElementById("chartDecibeis").getContext("2d");
const chartDecibeis = new Chart(ctxDecibeis, {
  type: "line",
  data: {
    labels,
    datasets: [
      {
        label: "Valor FC-04",
        data: FC04decibeisData,
        borderColor: "#d90429",
        backgroundColor: getGradient(ctxDecibeis, "#ffb3c1", "#fff0f3"),
        fill: true,
        tension: 0.4,
        pointRadius: 5,
        pointBackgroundColor: "#d90429",
        pointBorderColor: "#fff",
        pointHoverRadius: 7,
        borderWidth: 3,
      },
    ],
  },
  options: {
    responsive: true,
    animation: {
      duration: 1200,
      easing: "easeOutQuart",
    },
    plugins: {
      legend: { position: "top", labels: { font: { size: 16 } } },
      title: {
        display: true,
        text: "Decibeis Detectados (FC-04)",
        font: { size: 20, weight: "bold" },
        color: "#d90429",
      },
      tooltip: {
        backgroundColor: "#fff",
        titleColor: "#d90429",
        bodyColor: "#333",
        borderColor: "#d90429",
        borderWidth: 1,
        padding: 12,
      },
    },
    scales: {
      y: {
        beginAtZero: true,
        grid: { color: "#ffb3c1" },
        ticks: { color: "#d90429", font: { size: 14 } },
      },
      x: {
        grid: { color: "#ffb3c1" },
        ticks: { color: "#d90429", font: { size: 14 } },
      },
    },
  },
  plugins: [shadowLinePlugin],
});

let intervalId = null;

/**
 * Busca dados dos sensores do dispositivo ESP32 e atualiza os elementos da interface e gráficos.
 *
 * Esta função realiza as seguintes ações:
 * - Envia uma requisição fetch para o dispositivo ESP32 usando o endereço IP fornecido.
 * - Analisa a resposta JSON contendo dados dos sensores como distância interna/externa, contagem de pessoas, peso e detecção de gás.
 * - Atualiza os elementos DOM correspondentes com os valores mais recentes dos sensores.
 * - Mantém e atualiza os arrays de dados para visualização nos gráficos, garantindo um número máximo de pontos.
 * - Atualiza os gráficos para refletir os novos dados.
 * - Atualiza o indicador de status de conexão com base no sucesso ou falha da operação fetch.
 *
 * @async
 * @function fetchData
 * @returns {Promise<void>} Resolvido quando os dados forem buscados e a interface atualizada.
 */
async function fetchData() {
  if (!ip) return;
  try {
    const res = await fetch(ip, { mode: "cors" });
    const data = await res.json();
    FC04decibeisE1.textContent = data.FC04decibeis + " db";
    const now = new Date().toLocaleTimeString();
    if (labels.length >= MAX_POINTS) {
      labels.shift();
      FC04decibeisData.shift();
    }
    labels.push(now);
    FC04decibeisData.push(data.FC04decibeis);
    chartDecibeis.update();
    statusEl.textContent = `Conectado - Última atualização: ${now}`;
    statusEl.style.color = "green";
    const dados = {
      horario: new Date().toLocaleTimeString(),
      FC04decibeis: data.FC04decibeis,
    };
  } catch (error) {
    statusEl.textContent = "Erro de conexão com ESP32";
    statusEl.style.color = "red";
    console.error("Erro ao buscar dados do ESP32:", error);
  }
}

// ? Evento de clique no botão de conexão
document.getElementById("connectBtn").addEventListener("click", function () {
  const ipValue = document.getElementById("ipInput").value.trim();
  if (!ipValue) {
    statusEl.textContent = "Por favor, insira um IP válido.";
    statusEl.style.color = "orange";
    return;
  }
  ip = `http://${ipValue}/`;
  statusEl.textContent = "Conectando...";
  statusEl.style.color = "blue";
  labels.length = 0;
  FC04decibeisData.length = 0;
  chartDecibeis.update();
  if (intervalId) clearInterval(intervalId);
  intervalId = setInterval(fetchData, 2000);
  fetchData();
});
