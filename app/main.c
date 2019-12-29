#include "includes.h"
#include <avr/io.h>
#include <util/delay.h>
#include <string.h>

#define  TASK_STK_SIZE  OS_TASK_DEF_STK_SIZE            
#define  N_TASKS        10

typedef struct {
	INT8U data, idx;
} mail;
//FND 메일박스 구조체

OS_STK TaskStk[N_TASKS][TASK_STK_SIZE];

OS_EVENT* FndMbox;   //FND의 mailbox
OS_EVENT* LedMbox;   //Led의 mailbox
OS_EVENT* mutex;   //mutex
OS_EVENT* TempMsgQ;      //온도의 message queue
OS_FLAG_GRP* event_flag_grp;      //task의 execution order를 정하는 이벤트 플레그
void* TempMsgQTbl[2];  //Message Queue Table


volatile INT8U on = 1;         //buzzer on,off
volatile INT8U cntt = 0;      //주기 횟수
volatile int time = 0;         //시간 체크
volatile int led_over = 0;      //빛 이상 체크 변수
volatile int temperature_over = 0;      //온도 이상 체크 변수
volatile int flag = 0;         //이상 체크
volatile int stop = 1;         //타이머 일시정지
volatile INT8U FndData[4];   // 각 자리에 들어갈 수 저장
const INT8U myMapTbl[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
const INT8U Num[12] = { 0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7c,0x07,0x7f,0x67,0x40,0x00 };      //FND 숫자 출력
int cnt[4];   // 시간 자리수 계산



void FireTask(void *pdata);         //화재 확인
void FndTask(void *pdata);         //FND 자릿수에 수 할당
void FndDisplayTask(void *pdata);   //FND 디스플레이
void LedTask(void *pdata);         //LED 디스플레이
void LedOperation(unsigned short value);   //LED 값 할당
void TemperatureTask(void *pdata);      //온도 체크 
void Read_TWI_Task(void *pdata);         //온도 읽기
void ReadLightTask(void *pdata);         //빛 읽기
void Init_TWI(void);               //TWI 초기화
ISR(INT4_vect);                     //SW1 사용
ISR(INT5_vect);                     //SW2 사용
ISR(TIMER2_OVF_vect);               // 타이머/카운터 2 사용



int main(void)
{
	INT8U err;
	OSInit();            //OS 초기화
	OS_ENTER_CRITICAL();   //임계영역 진입
	TCCR0 = 0x05;         //타이머0 128분주 설정
	TCCR2 = 0x05;         //타이머2 128분주 설정
	TIMSK = 0x41;         //타이머0,2 오버플로우 인터럽트 설정
	DDRE = 0xCF;   //SW 입출력 방향 설정
	EICRB = 0x0A;   //SW 하강 엣지 설정
	EIMSK = 0x30;   //스위치 4,5번 외부 인터럽트 허용
	sei();         //외부 인터럽트 허용
	OS_EXIT_CRITICAL();      //임계영역 탈출

	FndMbox = OSMboxCreate((void*)0);      //FND 메일박스 생성
	LedMbox = OSMboxCreate((void*)0);      //LED 메일박스 생성
	mutex = OSMutexCreate(1, &err);         //
	event_flag_grp = OSFlagCreate(0x00, &err);      //이벤트 플래그 생성
	TempMsgQ = OSQCreate(TempMsgQTbl, 2);      //온도 메시지 큐 생성

	OSTaskCreate(FireTask, (void *)0, (void *)&TaskStk[0][TASK_STK_SIZE - 1], 2);
	OSTaskCreate(TemperatureTask, (void *)0, (void *)&TaskStk[1][TASK_STK_SIZE - 1], 3);
	OSTaskCreate(Read_TWI_Task, (void *)0, (void *)&TaskStk[2][TASK_STK_SIZE - 1], 4);
	OSTaskCreate(ReadLightTask, (void *)0, (void *)&TaskStk[3][TASK_STK_SIZE - 1], 5);
	OSTaskCreate(LedTask, (void *)0, (void *)&TaskStk[4][TASK_STK_SIZE - 1], 6);
	OSTaskCreate(FndDisplayTask, (void *)0, (void *)&TaskStk[5][TASK_STK_SIZE - 1], 7);
	OSTaskCreate(FndTask, (void *)0, (void *)&TaskStk[6][TASK_STK_SIZE - 1], 8);
	//태스크 생성

	OSStart();               //OS 시작

	return 0;
}

ISR(INT4_vect) {                        //스위치 1
	if (led_over || temperature_over) {            //일시정지
		stop = 1 - stop;
	}
}


ISR(INT5_vect) {                     //스위치 2,  초기화 버튼
	int err;
	led_over = 0;
	temperature_over = 0;
	time = 0;
	stop = 1;
	OSFlagPost(event_flag_grp, 0x80, OS_FLAG_SET, &err);
}

ISR(TIMER2_OVF_vect) {      //타이머 2작동
	int err;
	if ((stop == 1) && (led_over) && (temperature_over)) {      //화재 발생시 실행
		if (++cntt == 62) {                        //1초 카운트
			time++;
			cntt = 0;
			TCNT2 = 0x00;
		}

		cnt[3] = (time / 600) % 10;               //fnd 첫자리
		cnt[2] = (time / 60) % 10;               //fnd 두번째자리
		cnt[1] = (time % 60) / 10;               //fnd 세번째
		cnt[0] = time % 10;                     //fnd 네번째 자리


		FndData[3] = Num[cnt[3]];
		FndData[2] = Num[cnt[2]] | 0x80;
		FndData[1] = Num[cnt[1]];
		FndData[0] = Num[cnt[0]];

		if (on) {
			PORTB = 0x00;                  //버저 on
			on = 0;
			OSTimeDlyHMSM(0, 0, 0, 2);
		}
		else {
			PORTB = 0x10;                  //버저 off
			on = 1;
			OSTimeDlyHMSM(0, 0, 0, 2);
		}


	}
}


void FireTask(void *pdata) {
	INT8U err, i;
	while (1) {
		for (i = 0; i < 2; i++) {
			OSFlagPend(event_flag_grp, 0x03, OS_FLAG_WAIT_SET_ANY + OS_FLAG_CONSUME, 0, &err);         //flag 기다림
			if (led_over && temperature_over) {
				OSFlagPend(event_flag_grp, 0x80, OS_FLAG_WAIT_SET_ALL + OS_FLAG_CONSUME, 0, &err);      // 화재 확인
			}
		}
		led_over = 0;
		temperature_over = 0;
		OSFlagPost(event_flag_grp, 0x30, OS_FLAG_SET, &err);            //플래그 보내기
	}
}




void FndTask(void *pdata) {               //Fnd에 들어갈 수 결정
	INT8U idx = 0;
	mail fnd_data;
	while (1) {
		fnd_data.idx = idx; // FND 위치결정
		fnd_data.data = FndData[idx]; // 표시할 수
		idx = (idx + 1) & 0x03;
		OSMboxPost(FndMbox, &fnd_data);   //메일박스로 전송(FndDisplayTask로)
		OSTimeDlyHMSM(0, 0, 0, 2);   //2ms delay
	}
}

void FndDisplayTask(void *pdata)
{
	INT8U err;
	DDRC = 0xff;   //FND 출력포트 출력모드
	DDRG = 0x0f;   //FND 선택포트 출력모드
	mail fnd_data;            //fnd_data 구조체 선언
	while (1) {
		fnd_data = *(mail*)OSMboxPend(FndMbox, 0, &err);   //메일박스 받음
		PORTG = myMapTbl[fnd_data.idx];   //출력할 위치결정
		PORTC = fnd_data.data;            //FND 출력
	}
}

void LedOperation(unsigned short value)
{
	value = value < 100 ? 0 : value - 100;         //빛의 세기 200부터 900까지 LED 게이지 할당  (900이상일 경우 LED전부 켜기)
	unsigned short bit = value / 100;

	int i = 0;
	for (i = 0; i < bit; i++) {
		PORTA |= 1 << i;                  //LED on
	}
}

void LedTask(void *pdata)
{
	INT8U err;
	unsigned short value;

	DDRA = 0xFF;      //LED 출력모드
	while (1) {
		PORTA = 0x00;//LED초기화
		value = *(unsigned short*)OSMboxPend(LedMbox, 0, &err);   //ReadLightTask에서 메일박스 기다림
		LedOperation(value);   //LED 게이지 계산
		if (value < 300) {
			led_over = 1;         //밝기가 300 미만일 경우 화재 발생
		}
		OSFlagPost(event_flag_grp, 0x01, OS_FLAG_SET, &err);      //Fire task에 task 마침 알림
		OSTimeDlyHMSM(0, 0, 0, 50);
	}
}




void ReadLightTask(void *pdata) {      //빛의 세기를 읽는 task
	INT8U err;
	unsigned char low, high;
	unsigned short value;
	ADMUX = 0x00;   //ADMUX 설정
	ADCSRA = 0x87;   //ADCSRA 설정, 프리스케일러 128분주
	while (1) {
		ADCSRA |= 0x40;   //ADC 변환 시작
		while ((ADCSRA & 0x10) != 0x10);   //변환 완료까지 대기
		low = ADCL;
		high = ADCH;
		value = (high << 8) | low;   //low 와 high 조합
		OSMboxPost(LedMbox, &value);      // 빛의 세기를 LedTask로 보냄

		OSFlagPend(event_flag_grp, 0x10, OS_FLAG_WAIT_SET_ANY + OS_FLAG_CONSUME, 0, &err);   //플래그가 0x10이  set될때까지 기다림
		OSTimeDlyHMSM(0, 0, 0, 50);
	}
}


void Init_TWI()
{
	PORTD = 3;                   // For Internal pull-up for SCL & SCK
	SFIOR &= ~(1 << PUD);          // PUD=0: Pull Up Disable
	TWSR = 0;                   // 프리스케일러 =00 (1배)
	TWBR = 32;                  // bit trans rate : for 100  K Hz bus clock
	TWCR = _BV(TWEA) | _BV(TWEN);   // TWEA으로 Ack pulse 생성
							// TWEN으로 TWI 동작 enable 설정
}

void TemperatureTask(void *pdata)
{
	INT8U err;
	INT8U low, high;
	int val;
	INT8U val_int, val_deci;      //정수부분, 소수 부분
	while (1) {
		low = 0;
		high = 0;
		low = *(INT8U*)OSQPend(TempMsgQ, 0, &err);
		high = *(INT8U*)OSQPend(TempMsgQ, 0, &err);
		//메시지큐에서 값 받음
		val = high;
		val <<= 8;         //상위 8비트 설정
		val |= low;         //하위 8비트 설정

		OSMutexPend(mutex, 1, &err);
		if (err) {                     //mutex 사용중일경우 interrupt 무시
			return;
		}

		if ((val & 0x8000) != 0x8000) {
			FndData[3] = Num[11];   //양수일 경우 표시안함
		}
		else {
			FndData[3] = Num[10];   //음수이면 - 표시
			val = (~val) - 1;      //양수로 변경
		}
		val_int = (INT8U)((val & 0x7f00) >> 8);         //정수부분
		val_deci = (INT8U)(val & 0x00ff);            //실수 부분
		if (val_int >= 28) {
			temperature_over = 1;         //28도가 넘어가면 화재발생
		}

		FndData[2] = Num[(val_int / 10) % 10];            //정수부분 10의자리
		FndData[1] = (Num[val_int % 10] | 0x80);         //정수부분 1의자리
		FndData[0] = Num[((val_deci & 0x80) == 0x80) * 5];      //실수부분
		OSMutexPost(mutex);               //mutex 반환
		OSFlagPost(event_flag_grp, 0x02, OS_FLAG_SET, &err);   //Fire task에 task 마침 알림
		OSTimeDlyHMSM(0, 0, 0, 50);
	}
}

void Read_TWI_Task(void *pdata)
{
	INT8U err;
	INT8U low, high;

	Init_TWI();

	while (1) {
		OS_ENTER_CRITICAL();         //임계영역 진입
		TWCR = _BV(TWSTA) | _BV(TWINT) | _BV(TWEN);   // START 전송
		while (!(TWCR & _BV(TWINT)));            // START 상태 검사, 이후 ACK 및 상태 검사

		TWDR = 0x98 + 1;                      // TWDR=TEMP_I2C_ADDR + 1 => SLA+R 준비, R=1
		TWCR = _BV(TWINT) | _BV(TWEN);            // SLA+R 전송
		while (!(TWCR & _BV(TWINT)));            // ACK를 기다림

		TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWEA);   // 1st DATA 준비
		while (!(TWCR & _BV(TWINT)));            // ACK를 기다림
		high = TWDR;                        // 첫번째 1 byte DATA 수신
		TWCR = _BV(TWINT) | _BV(TWEN);             // SLA+R 전송
		while (!(TWCR & _BV(TWINT)));            // ACK를 기다림
		low = TWDR;                        // 2번째 1 byte DATA 수신
		TWCR = _BV(TWINT) | _BV(TWEN) | _BV(TWSTO);   // STOP 전송
		//온도센서는 16bit 기준으로 값을 가져오므로 8비트씩 2번을 수신해야한다
		TIMSK = ((((int)high << 8) | low) >= 33) ? TIMSK | _BV(TOIE2) : TIMSK & ~_BV(TOIE2);
		//STOP 전송
		OS_EXIT_CRITICAL();         //임계영역 탈출
		OSQPost(TempMsgQ, &low);      //Temperature task에 low를 메시지 큐로 보냄
		OSQPost(TempMsgQ, &high);      //Temperature task에 high를 메시지 큐로 보냄
		OSFlagPend(event_flag_grp, 0x20, OS_FLAG_WAIT_SET_ANY + OS_FLAG_CONSUME, 0, &err);
		//Fire task에 완료 플래그 보냄
		OSTimeDlyHMSM(0, 0, 0, 50);
	}
}