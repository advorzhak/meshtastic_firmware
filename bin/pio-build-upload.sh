#!/usr/bin/env bash

# Select a PlatformIO environment and run clean/build/upload via the repository devcontainer.

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORKSPACE_IN_CONTAINER="/workspaces/meshtastic_firmware"

CONTAINER_NAME_BASE="${DEVCONTAINER_CONTAINER_NAME:-meshtastic-firmware-devcontainer}"
RUN_ID="$(date +%s)-$$"
CONTAINER_NAME="${CONTAINER_NAME_BASE}-${RUN_ID}"
IMAGE_TAG="${DEVCONTAINER_IMAGE_TAG:-meshtastic-firmware-devcontainer:local}"
DOCKERFILE_PATH="${REPO_ROOT}/.devcontainer/Dockerfile"
DOCKER_CONTEXT="${REPO_ROOT}/.devcontainer"
SKIP_UPLOAD=false
SELECTED_ENV=""
NON_INTERACTIVE=false

usage() {
	cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Select a PlatformIO environment and run clean/build/upload via the repository devcontainer.

Options:
  -e, --env ENV        Non-interactive: select environment by name and run immediately
  --no-upload          Build only, skip upload step
  --build-only         Alias for --no-upload
  -h, --help           Show this help message
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	-e | --env)
		if [[ -z ${2:-} ]]; then
			printf "${RED}Error: --env requires an argument${NC}\n" >&2
			exit 1
		fi
		SELECTED_ENV="$2"
		NON_INTERACTIVE=true
		shift 2
		;;
	--no-upload | --build-only)
		SKIP_UPLOAD=true
		shift
		;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		printf "${RED}Error: unknown argument '%s'.${NC}\n" "$1" >&2
		usage >&2
		exit 1
		;;
	esac
done

cleanup_container() {
	if command -v docker >/dev/null 2>&1; then
		docker rm -f "${CONTAINER_NAME}" >/dev/null 2>&1 || true
	fi
}

trap cleanup_container EXIT INT TERM

printf "${BLUE}=== PlatformIO Build & Upload (Devcontainer) ===${NC}\n\n"

HOST_IS_DARWIN=false
[[ "$(uname -s)" == "Darwin" ]] && HOST_IS_DARWIN=true

detect_upload_port() {
	if [[ -n ${DEVCONTAINER_UPLOAD_DEVICE:-} ]]; then
		printf "%s" "${DEVCONTAINER_UPLOAD_DEVICE}"
		return
	fi

	# Prefer pio's device list; drop macOS pseudo-ports that sort before real modems.
	if command -v pio >/dev/null 2>&1; then
		local pio_port=""
		if command -v jq >/dev/null 2>&1; then
			# shellcheck disable=SC2155
			pio_port=$(pio device list --json 2>/dev/null |
				jq -r '.[] | select(.port) | .port' 2>/dev/null |
				grep -vE 'debug-console|Bluetooth|wlan' |
				head -1 || true)
		else
			# shellcheck disable=SC2155
			pio_port=$(pio device list --json 2>/dev/null |
				python3 -c "import sys,json; ports=[d['port'] for d in json.load(sys.stdin) if 'port' in d]; print('\n'.join(ports))" 2>/dev/null |
				grep -vE 'debug-console|Bluetooth|wlan' |
				head -1 || true)
		fi
		if [[ -n ${pio_port} ]]; then
			printf "%s" "${pio_port}"
			return
		fi
	fi

	local port=""
	for glob in '/dev/cu.usbmodem*' '/dev/cu.SLAB_USB*' '/dev/cu.usbserial*' \
		'/dev/ttyACM*' '/dev/ttyUSB*'; do
		# shellcheck disable=SC2086
		for port in ${glob}; do
			if [[ -e ${port} ]]; then
				printf "%s" "${port}"
				return
			fi
		done
	done

	return 0
}

choose_upload_port() {
	local auto_port=""
	auto_port="$(detect_upload_port)"

	if [[ -n ${auto_port} ]]; then
		printf "${GREEN}Auto-detected upload port: ${auto_port}${NC}\n"
		printf "${YELLOW}Use this port? [Y/n/other port]: ${NC}"
		read -r port_choice
		port_choice="${port_choice:-Y}"
		if [[ ${port_choice} =~ ^[Yy]$ ]]; then
			upload_port="${auto_port}"
		elif [[ ${port_choice} =~ ^[Nn]$ ]]; then
			printf "${YELLOW}Enter upload port manually: ${NC}"
			read -r upload_port
		else
			upload_port="${port_choice}"
		fi
	else
		printf "${YELLOW}No serial port detected automatically.\n"
		if [[ ${HOST_IS_DARWIN} == "true" ]]; then
			printf "Enter upload port (e.g. /dev/cu.usbmodem1234), or leave empty to let pio try: ${NC}"
		else
			printf "Enter upload port (e.g. /dev/ttyUSB0) so it can be mounted before the devcontainer starts: ${NC}"
		fi
		read -r upload_port
	fi
}

upload_port=""
if [[ ${SKIP_UPLOAD} != "true" && ${HOST_IS_DARWIN} != "true" ]]; then
	printf "${BLUE}Selecting upload port before starting the devcontainer so it can be mounted.${NC}\n"
	choose_upload_port
	if [[ -z ${upload_port} ]]; then
		printf "${RED}Error: Linux upload requires a selected serial port before the devcontainer starts.${NC}\n"
		exit 1
	fi
	export DEVCONTAINER_UPLOAD_DEVICE="${upload_port}"
fi

run_in_devcontainer() {
	local cmd="$1"
	docker exec -i "${CONTAINER_NAME}" bash -lc "export PATH=\"\$HOME/.local/bin:\$PATH\"; cd \"${WORKSPACE_IN_CONTAINER}\"; ${cmd}"
}

ensure_devcontainer() {
	if ! command -v docker >/dev/null 2>&1; then
		printf "${RED}Error: docker is required but was not found in PATH.${NC}\n"
		exit 1
	fi

	if ! docker info >/dev/null 2>&1; then
		printf "${RED}Error: docker daemon is not reachable.${NC}\n"
		printf "${YELLOW}If you use Colima, start it first (e.g. 'colima start').${NC}\n"
		exit 1
	fi

	printf "${YELLOW}Building devcontainer image: ${IMAGE_TAG}${NC}\n"
	docker build -f "${DOCKERFILE_PATH}" -t "${IMAGE_TAG}" "${DOCKER_CONTEXT}"

	local -a extra_args=()
	if [[ -n ${DEVCONTAINER_DOCKER_RUN_ARGS:-} ]]; then
		read -r -a extra_args <<<"${DEVCONTAINER_DOCKER_RUN_ARGS}"
	elif [[ -d /dev/bus/usb ]]; then
		extra_args=(-v /dev/bus/usb:/dev/bus/usb)
	fi

	if [[ -n ${DEVCONTAINER_UPLOAD_DEVICE:-} ]]; then
		extra_args+=(--device "${DEVCONTAINER_UPLOAD_DEVICE}")
	fi

	printf "${YELLOW}Creating isolated devcontainer: ${CONTAINER_NAME}${NC}\n"
	docker run -d --name "${CONTAINER_NAME}" --init \
		-v "${REPO_ROOT}:${WORKSPACE_IN_CONTAINER}" \
		-w "${WORKSPACE_IN_CONTAINER}" \
		"${extra_args[@]}" \
		"${IMAGE_TAG}" tail -f /dev/null >/dev/null

	run_in_devcontainer "git config --global --add safe.directory \"${WORKSPACE_IN_CONTAINER}\""
	run_in_devcontainer "PIP_BREAK_SYSTEM_PACKAGES=1 .devcontainer/setup.sh"
}

ensure_devcontainer

printf "${YELLOW}Fetching available environments...${NC}\n"
ENVS=()
while IFS= read -r env; do
	[[ -n ${env} ]] && ENVS+=("${env}")
done < <(run_in_devcontainer "pio project config --json-output 2>/dev/null" |
	jq -r '.[].env' 2>/dev/null |
	grep -E '^(t-echo(-inkhud)?|tbeam-s3-core)$')

if [[ ${#ENVS[@]} -eq 0 ]]; then
	printf "${RED}Error: No matching environments found.${NC}\n"
	exit 1
fi

if [[ ${NON_INTERACTIVE} == "true" ]]; then
	if ! printf '%s\n' "${ENVS[@]}" | grep -qxF "${SELECTED_ENV}"; then
		printf "${RED}Error: Environment '${SELECTED_ENV}' not found. Available:${NC}\n"
		printf '%s\n' "${ENVS[@]}"
		exit 1
	fi
else
	printf "\n${GREEN}Available environments:${NC}\n"
	for i in "${!ENVS[@]}"; do
		printf "%3d) %s\n" "$((i + 1))" "${ENVS[$i]}"
	done

	printf "\n${YELLOW}Enter the number of the environment to build (or 'q' to quit):${NC}\n"
	read -r -p "> " selection

	if [[ ${selection} == "q" || ${selection} == "Q" ]]; then
		printf "${BLUE}Exiting...${NC}\n"
		exit 0
	fi

	if ! [[ ${selection} =~ ^[0-9]+$ ]] || [[ ${selection} -lt 1 ]] || [[ ${selection} -gt ${#ENVS[@]} ]]; then
		printf "${RED}Error: Invalid selection.${NC}\n"
		exit 1
	fi

	SELECTED_ENV="${ENVS[$((selection - 1))]}"
fi

printf "\n${GREEN}Selected environment: ${SELECTED_ENV}${NC}\n"

printf "\n${BLUE}========================================${NC}\n"
printf "${BLUE}Step 1/3: Cleaning environment...${NC}\n"
printf "${BLUE}========================================${NC}\n"
run_in_devcontainer "pio run -e \"${SELECTED_ENV}\" -t clean"

printf "\n${BLUE}========================================${NC}\n"
printf "${BLUE}Step 2/3: Building firmware...${NC}\n"
printf "${BLUE}========================================${NC}\n"
run_in_devcontainer "pio run -e \"${SELECTED_ENV}\""

if [[ ${SKIP_UPLOAD} == "true" ]]; then
	printf "\n${YELLOW}Upload skipped (--no-upload). Build completed successfully.${NC}\n"
	exit 0
fi

printf "\n${BLUE}========================================${NC}\n"
printf "${BLUE}Step 3/3: Uploading firmware...${NC}\n"
printf "${BLUE}========================================${NC}\n"
printf "${YELLOW}Ready to upload firmware to device. Continue? [Y/n]: ${NC}"
read -r upload_confirm
upload_confirm="${upload_confirm:-Y}"

if [[ ! ${upload_confirm} =~ ^[Yy]$ ]]; then
	printf "${YELLOW}Upload cancelled. Build completed successfully.${NC}\n"
	exit 0
fi

if [[ ${HOST_IS_DARWIN} == "true" ]]; then
	if [[ -z ${upload_port} ]]; then
		choose_upload_port
	fi
fi

if [[ ${HOST_IS_DARWIN} == "true" ]] && ! command -v pio >/dev/null 2>&1; then
	printf "${RED}Error: 'pio' is required on macOS for upload but was not found in PATH.${NC}\n"
	printf "${YELLOW}Build steps run in the isolated devcontainer, but macOS upload must run on host because Docker cannot access /dev/cu.* devices.${NC}\n"
	printf "${YELLOW}Install PlatformIO on host or use --no-upload to build only.${NC}\n"
	exit 1
fi

if [[ ${HOST_IS_DARWIN} == "true" ]] && command -v pio >/dev/null 2>&1; then
	printf "${BLUE}Uploading from host (macOS: serial device not accessible inside container)...${NC}\n"
	printf "${YELLOW}Using PlatformIO upload path on host (may rebuild if build flags changed).${NC}\n"
	upload_cmd=(pio run -e "${SELECTED_ENV}" -t upload)
	if [[ -n ${upload_port} ]]; then
		upload_cmd+=(--upload-port "${upload_port}")
	fi
	cd "${REPO_ROOT}" && "${upload_cmd[@]}"
else
	# Linux host or pio not on host: run inside container (device must be passed through).
	if [[ -z ${upload_port} ]]; then
		printf "${RED}Error: Linux upload requires a selected serial port before the devcontainer starts.${NC}\n"
		exit 1
	fi
	run_in_devcontainer "pio run -e \"${SELECTED_ENV}\" -t upload --upload-port \"${upload_port}\""
fi

printf "\n${GREEN}========================================${NC}\n"
printf "${GREEN}All steps completed successfully!${NC}\n"
printf "${GREEN}========================================${NC}\n"
